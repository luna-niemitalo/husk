#include "db2.hpp"

#include <algorithm>
#include <cstring>

namespace husk::db2 {

namespace {

// IN: `buf`/`pos`/`size` -- caller-declared read, foreign until this
// bounds check passes. Every fixed-field read in this file goes through
// one of these two helpers so the "does it fit" check never gets
// duplicated ad hoc at a call site.
void requireBytes(size_t pos, size_t need, size_t bufSize, const char* what) {
    if (pos > bufSize || need > bufSize - pos) {
        throw ParseError(std::string("db2: truncated ") + what + ": need " +
                          std::to_string(need) + " bytes at offset " + std::to_string(pos) +
                          ", buffer is " + std::to_string(bufSize) + " bytes");
    }
}

template <typename T>
T readLE(const std::vector<uint8_t>& buf, size_t pos, const char* what) {
    requireBytes(pos, sizeof(T), buf.size(), what);
    T value;
    std::memcpy(&value, buf.data() + pos, sizeof(T));
    return value;
}

uint32_t readU32(const std::vector<uint8_t>& buf, size_t pos, const char* what) {
    return readLE<uint32_t>(buf, pos, what);
}
uint16_t readU16(const std::vector<uint8_t>& buf, size_t pos, const char* what) {
    return readLE<uint16_t>(buf, pos, what);
}
uint64_t readU64(const std::vector<uint8_t>& buf, size_t pos, const char* what) {
    return readLE<uint64_t>(buf, pos, what);
}

constexpr size_t kMagicSize = 4;
constexpr size_t kSchemaStringSize = 128;

// Header, up to and including section_count -- everything after the fixed
// magic+versionNum+schemaString prefix. 19 uint32/uint16 fields, per
// DB2.md's wdc5_db2_header (id_index sits as the low half of one uint32
// slot, packed with flags -- read as two uint16 below, matching the header's
// documented `uint16_t flags; uint16_t id_index;` pair).
Header parseHeader(const std::vector<uint8_t>& buf, size_t& pos) {
    requireBytes(0, kMagicSize, buf.size(), "WDC5 magic");
    std::string magic(reinterpret_cast<const char*>(buf.data()), kMagicSize);
    if (magic != "WDC5") {
        throw ParseError("db2: expected magic 'WDC5', got '" + magic +
                          "' -- this parser only implements the WDC5 container "
                          "(see db2.hpp's module comment)");
    }
    pos = kMagicSize;

    Header h;
    h.versionNum = readU32(buf, pos, "header.versionNum");
    pos += 4;

    requireBytes(pos, kSchemaStringSize, buf.size(), "header.schemaString");
    const char* schemaBytes = reinterpret_cast<const char*>(buf.data() + pos);
    size_t schemaLen = strnlen(schemaBytes, kSchemaStringSize);
    h.schemaString.assign(schemaBytes, schemaLen);
    pos += kSchemaStringSize;

    h.recordCount = readU32(buf, pos, "header.recordCount");
    pos += 4;
    h.fieldCount = readU32(buf, pos, "header.fieldCount");
    pos += 4;
    h.recordSize = readU32(buf, pos, "header.recordSize");
    pos += 4;
    h.stringTableSize = readU32(buf, pos, "header.stringTableSize");
    pos += 4;
    h.tableHash = readU32(buf, pos, "header.tableHash");
    pos += 4;
    h.layoutHash = readU32(buf, pos, "header.layoutHash");
    pos += 4;
    h.minId = readU32(buf, pos, "header.minId");
    pos += 4;
    h.maxId = readU32(buf, pos, "header.maxId");
    pos += 4;
    h.locale = readU32(buf, pos, "header.locale");
    pos += 4;
    h.flags = readU16(buf, pos, "header.flags");
    pos += 2;
    h.idIndex = readU16(buf, pos, "header.idIndex");
    pos += 2;
    h.totalFieldCount = readU32(buf, pos, "header.totalFieldCount");
    pos += 4;
    h.bitpackedDataOffset = readU32(buf, pos, "header.bitpackedDataOffset");
    pos += 4;
    h.lookupColumnCount = readU32(buf, pos, "header.lookupColumnCount");
    pos += 4;
    h.fieldStorageInfoSize = readU32(buf, pos, "header.fieldStorageInfoSize");
    pos += 4;
    h.commonDataSize = readU32(buf, pos, "header.commonDataSize");
    pos += 4;
    h.palletDataSize = readU32(buf, pos, "header.palletDataSize");
    pos += 4;
    h.sectionCount = readU32(buf, pos, "header.sectionCount");
    pos += 4;

    return h;
}

SectionHeader parseSectionHeader(const std::vector<uint8_t>& buf, size_t& pos) {
    SectionHeader s;
    s.tactKeyHash = readU64(buf, pos, "sectionHeader.tactKeyHash");
    pos += 8;
    s.fileOffset = readU32(buf, pos, "sectionHeader.fileOffset");
    pos += 4;
    s.recordCount = readU32(buf, pos, "sectionHeader.recordCount");
    pos += 4;
    s.stringTableSize = readU32(buf, pos, "sectionHeader.stringTableSize");
    pos += 4;
    s.offsetRecordsEnd = readU32(buf, pos, "sectionHeader.offsetRecordsEnd");
    pos += 4;
    s.idListSize = readU32(buf, pos, "sectionHeader.idListSize");
    pos += 4;
    s.relationshipDataSize = readU32(buf, pos, "sectionHeader.relationshipDataSize");
    pos += 4;
    s.offsetMapIdCount = readU32(buf, pos, "sectionHeader.offsetMapIdCount");
    pos += 4;
    s.copyTableCount = readU32(buf, pos, "sectionHeader.copyTableCount");
    pos += 4;
    return s;
}

FieldStructure parseFieldStructure(const std::vector<uint8_t>& buf, size_t& pos) {
    FieldStructure f;
    f.size = readLE<int16_t>(buf, pos, "fieldStructure.size");
    pos += 2;
    f.position = readU16(buf, pos, "fieldStructure.position");
    pos += 2;
    return f;
}

// field_storage_info is a fixed 24-byte record regardless of storageType
// (2+2+4+4 header fields + 3*4 tagged-union payload, DB2.md's WDC2+
// section) -- the union's meaning is picked apart by storageType below,
// never re-derived elsewhere.
FieldStorageInfo parseFieldStorageInfo(const std::vector<uint8_t>& buf, size_t& pos) {
    FieldStorageInfo f;
    f.fieldOffsetBits = readU16(buf, pos, "fieldStorageInfo.fieldOffsetBits");
    pos += 2;
    f.fieldSizeBits = readU16(buf, pos, "fieldStorageInfo.fieldSizeBits");
    pos += 2;
    f.additionalDataSize = readU32(buf, pos, "fieldStorageInfo.additionalDataSize");
    pos += 4;
    uint32_t storageTypeRaw = readU32(buf, pos, "fieldStorageInfo.storageType");
    pos += 4;
    if (storageTypeRaw > static_cast<uint32_t>(FieldCompression::BitpackedSigned)) {
        throw ParseError("db2: field_storage_info.storageType " + std::to_string(storageTypeRaw) +
                          " is outside the documented field_compression range (0-5)");
    }
    f.storageType = static_cast<FieldCompression>(storageTypeRaw);

    uint32_t word1 = readU32(buf, pos, "fieldStorageInfo union word 1");
    uint32_t word3 = readU32(buf, pos + 8, "fieldStorageInfo union word 3");
    (void)word1;
    switch (f.storageType) {
        case FieldCompression::Bitpacked:
        case FieldCompression::BitpackedSigned:
            f.signExtend = (word3 & 0x01) != 0;
            break;
        case FieldCompression::CommonData:
            f.defaultValue = word1;
            break;
        case FieldCompression::BitpackedIndexedArray:
            f.arrayCount = word3;
            break;
        default:
            break;
    }
    pos += 12;
    return f;
}

}  // namespace

File parse(const std::vector<uint8_t>& buf) {
    File file;
    size_t pos = 0;
    file.header = parseHeader(buf, pos);
    const Header& h = file.header;

    std::vector<SectionHeader> sectionHeaders;
    sectionHeaders.reserve(h.sectionCount);
    for (uint32_t i = 0; i < h.sectionCount; ++i) {
        sectionHeaders.push_back(parseSectionHeader(buf, pos));
    }

    file.fieldStructures.reserve(h.totalFieldCount);
    for (uint32_t i = 0; i < h.totalFieldCount; ++i) {
        file.fieldStructures.push_back(parseFieldStructure(buf, pos));
    }

    if (h.fieldStorageInfoSize % 24 != 0) {
        throw ParseError("db2: header.fieldStorageInfoSize " +
                          std::to_string(h.fieldStorageInfoSize) +
                          " is not a multiple of the 24-byte field_storage_info record size");
    }
    uint32_t fieldStorageInfoCount = h.fieldStorageInfoSize / 24;
    file.fieldStorageInfo.reserve(fieldStorageInfoCount);
    for (uint32_t i = 0; i < fieldStorageInfoCount; ++i) {
        file.fieldStorageInfo.push_back(parseFieldStorageInfo(buf, pos));
    }

    requireBytes(pos, h.palletDataSize, buf.size(), "pallet_data");
    file.palletData.assign(buf.begin() + pos, buf.begin() + pos + h.palletDataSize);
    pos += h.palletDataSize;

    requireBytes(pos, h.commonDataSize, buf.size(), "common_data");
    file.commonData.assign(buf.begin() + pos, buf.begin() + pos + h.commonDataSize);
    pos += h.commonDataSize;

    // encrypted_status blocks (DB2.md's WDC4+ section): one per section
    // whose tact_key_hash is non-zero, sitting right here, before the
    // sections themselves. Skipped over -- husk has no TACT key store, so
    // an encrypted section's record bytes are opaque either way (see
    // Section::hasOffsetMap's tactKeyHash == 0 guard).
    for (uint32_t i = 0; i < h.sectionCount; ++i) {
        if (sectionHeaders[i].tactKeyHash == 0) continue;
        uint32_t encryptedIdCount = readU32(buf, pos, "encrypted_status.encrypted_id_count");
        pos += 4;
        requireBytes(pos, static_cast<size_t>(encryptedIdCount) * 4, buf.size(),
                     "encrypted_status.encrypted_id");
        pos += static_cast<size_t>(encryptedIdCount) * 4;
    }

    bool hasOffsetMap = (h.flags & 0x01) != 0;
    bool hasRelationshipData = (h.flags & 0x02) != 0;

    file.sections.reserve(h.sectionCount);
    for (uint32_t i = 0; i < h.sectionCount; ++i) {
        Section section;
        section.header = sectionHeaders[i];
        size_t sectionPos = section.header.fileOffset;

        if (!hasOffsetMap) {
            size_t bytes = static_cast<size_t>(section.header.recordCount) * h.recordSize;
            requireBytes(sectionPos, bytes, buf.size(), "section.records");
            section.recordBytes.assign(buf.begin() + sectionPos, buf.begin() + sectionPos + bytes);
            sectionPos += bytes;

            requireBytes(sectionPos, section.header.stringTableSize, buf.size(),
                         "section.stringBlock");
            section.stringBlock.assign(buf.begin() + sectionPos,
                                        buf.begin() + sectionPos + section.header.stringTableSize);
            sectionPos += section.header.stringTableSize;
        } else {
            requireBytes(sectionPos, section.header.offsetRecordsEnd - sectionPos, buf.size(),
                         "section.variableRecordData");
            sectionPos = section.header.offsetRecordsEnd;
        }

        size_t idListCount = section.header.idListSize / 4;
        section.idList.reserve(idListCount);
        for (size_t j = 0; j < idListCount; ++j) {
            section.idList.push_back(readU32(buf, sectionPos, "section.idList"));
            sectionPos += 4;
        }

        section.copyTable.reserve(section.header.copyTableCount);
        for (uint32_t j = 0; j < section.header.copyTableCount; ++j) {
            CopyTableEntry e;
            e.newId = readU32(buf, sectionPos, "section.copyTable.newId");
            sectionPos += 4;
            e.copiedId = readU32(buf, sectionPos, "section.copyTable.copiedId");
            sectionPos += 4;
            section.copyTable.push_back(e);
        }

        if (hasOffsetMap) {
            section.offsetMap.reserve(section.header.offsetMapIdCount);
            for (uint32_t j = 0; j < section.header.offsetMapIdCount; ++j) {
                OffsetMapEntry e;
                e.offset = readU32(buf, sectionPos, "section.offsetMap.offset");
                sectionPos += 4;
                e.size = readU16(buf, sectionPos, "section.offsetMap.size");
                sectionPos += 2;
                section.offsetMap.push_back(e);
            }

            section.variableRecordBytes.reserve(section.offsetMap.size());
            for (const OffsetMapEntry& e : section.offsetMap) {
                if (e.offset == 0) {
                    section.variableRecordBytes.emplace_back();  // no entry at this ID
                    continue;
                }
                requireBytes(e.offset, e.size, buf.size(), "section.variableRecordBytes entry");
                section.variableRecordBytes.emplace_back(buf.begin() + e.offset,
                                                          buf.begin() + e.offset + e.size);
            }
        }

        // relationship_data_size is present in the section header
        // regardless of the header-level 0x02 flag (DB2.md's per-section
        // field, distinct from the header's own "has relationship data"
        // bit) -- read it whenever it's declared non-zero, not gated on
        // hasRelationshipData, matching the wiki's own `if
        // (section_headers.relationship_data_size > 0)` guard.
        (void)hasRelationshipData;
        if (section.header.relationshipDataSize > 0) {
            requireBytes(sectionPos, section.header.relationshipDataSize, buf.size(),
                         "section.relationshipMap");
            sectionPos += section.header.relationshipDataSize;
        }

        if (hasOffsetMap) {
            section.offsetMapIdList.reserve(section.header.offsetMapIdCount);
            for (uint32_t j = 0; j < section.header.offsetMapIdCount; ++j) {
                section.offsetMapIdList.push_back(
                    readU32(buf, sectionPos, "section.offsetMapIdList"));
                sectionPos += 4;
            }
        }

        file.sections.push_back(std::move(section));
    }

    return file;
}

namespace {

// Extracts `sizeBits` bits starting at `offsetBits` (both counted from the
// start of `record`) as an unsigned value, per DB2.md's Bitpacked case:
// "read as a little-endian value, then shifted right by (offset & 7) and
// masked with ((1 << size) - 1)". `sizeBits` is capped at 64 here (no field
// in any known DB2 needs more, and a wider read would overflow the uint64_t
// result anyway).
uint64_t readBits(const std::vector<uint8_t>& record, uint32_t offsetBits, uint32_t sizeBits) {
    if (sizeBits == 0 || sizeBits > 64) {
        throw ParseError("db2: field_size_bits " + std::to_string(sizeBits) +
                          " is out of the supported 1-64 bit range");
    }
    size_t byteStart = offsetBits / 8;
    size_t byteLen = (sizeBits + (offsetBits & 7) + 7) / 8;
    if (byteStart + byteLen > record.size()) {
        throw ParseError("db2: bitpacked field at bit offset " + std::to_string(offsetBits) +
                          " (size " + std::to_string(sizeBits) + " bits) runs past the " +
                          std::to_string(record.size()) + "-byte record");
    }
    uint64_t raw = 0;
    for (size_t i = 0; i < byteLen && i < 8; ++i) {
        raw |= static_cast<uint64_t>(record[byteStart + i]) << (8 * i);
    }
    raw >>= (offsetBits & 7);
    if (sizeBits < 64) {
        raw &= (uint64_t{1} << sizeBits) - 1;
    }
    return raw;
}

// The row's real ID: from section.idList when present (header.flags & 0x04,
// "has non-inline IDs"), else read straight out of the record at
// header.idIndex (the common case -- the ID is just another inline field).
// Assumes the ID field itself is never CommonData/pallet-indexed-compressed
// -- true of every real DB2 checked so far (IDs are always plain or
// bitpacked inline ints), not something this format guarantees structurally.
uint32_t rowIdForRecord(const File& file, const Section& section, const std::vector<uint8_t>& record,
                         size_t recordIndex) {
    if (recordIndex < section.idList.size()) return section.idList[recordIndex];
    if (file.header.idIndex < file.fieldStorageInfo.size()) {
        const FieldStorageInfo& idInfo = file.fieldStorageInfo[file.header.idIndex];
        return static_cast<uint32_t>(readBits(record, idInfo.fieldOffsetBits, idInfo.fieldSizeBits));
    }
    return static_cast<uint32_t>(recordIndex);
}

// Sums additionalDataSize over every field before `fieldIndex` that shares
// `type` (CommonData -> common_data, BitpackedIndexed(Array) -> pallet_data)
// -- DB2.md: "these sections are in the same order as the field_info, so to
// find the offset, add up the additional_data_size of any previous fields
// which are stored in the same block".
uint32_t additionalDataOffset(const std::vector<FieldStorageInfo>& infos, size_t fieldIndex,
                               FieldCompression type) {
    uint32_t offset = 0;
    for (size_t i = 0; i < fieldIndex; ++i) {
        if (infos[i].storageType == type) offset += infos[i].additionalDataSize;
    }
    return offset;
}

uint32_t readPallet4(const std::vector<uint8_t>& palletData, uint32_t byteOffset,
                      const char* what) {
    return readU32(palletData, byteOffset, what);
}

}  // namespace

std::vector<uint64_t> decodeField(const File& file, const Section& section, size_t recordIndex,
                                   size_t fieldIndex) {
    if (fieldIndex >= file.fieldStorageInfo.size()) {
        throw ParseError("db2: field index " + std::to_string(fieldIndex) +
                          " out of range, file has " + std::to_string(file.fieldStorageInfo.size()) +
                          " fields");
    }
    if (recordIndex >= section.header.recordCount) {
        throw ParseError("db2: record index " + std::to_string(recordIndex) +
                          " out of range, section has " +
                          std::to_string(section.header.recordCount) + " records");
    }

    const FieldStorageInfo& info = file.fieldStorageInfo[fieldIndex];
    size_t recordStart = recordIndex * file.header.recordSize;
    requireBytes(recordStart, file.header.recordSize, section.recordBytes.size(),
                 "decodeField record slice");
    const uint8_t* recordPtr = section.recordBytes.data() + recordStart;
    std::vector<uint8_t> record(recordPtr, recordPtr + file.header.recordSize);

    switch (info.storageType) {
        case FieldCompression::None: {
            // Array length per DB2.md's "Further Quirks":
            // ArrayLength = fieldSizeBits / (32 - fieldStructure.size).
            // fieldIndex indexes fieldStorageInfo, which is declared to be
            // in the same order as fieldStructures (both header.
            // totalFieldCount-sized, per-field arrays) -- not re-sorted or
            // re-derived here.
            if (fieldIndex >= file.fieldStructures.size()) {
                throw ParseError("db2: fieldStorageInfo/fieldStructures length mismatch at index " +
                                  std::to_string(fieldIndex));
            }
            int16_t structSize = file.fieldStructures[fieldIndex].size;
            uint32_t elementBits = static_cast<uint32_t>(32 - structSize);
            if (elementBits == 0 || info.fieldSizeBits % elementBits != 0) {
                throw ParseError("db2: field " + std::to_string(fieldIndex) +
                                  " has fieldSizeBits " + std::to_string(info.fieldSizeBits) +
                                  " not a multiple of its element width " +
                                  std::to_string(elementBits));
            }
            uint32_t arrayLength = info.fieldSizeBits / elementBits;
            std::vector<uint64_t> values;
            values.reserve(arrayLength);
            for (uint32_t i = 0; i < arrayLength; ++i) {
                values.push_back(readBits(record, info.fieldOffsetBits + i * elementBits, elementBits));
            }
            return values;
        }
        case FieldCompression::Bitpacked:
        case FieldCompression::BitpackedSigned: {
            uint64_t raw = readBits(record, info.fieldOffsetBits, info.fieldSizeBits);
            if (info.signExtend && info.fieldSizeBits < 64) {
                uint64_t signBit = uint64_t{1} << (info.fieldSizeBits - 1);
                if (raw & signBit) raw |= ~((signBit << 1) - 1);
            }
            return {raw};
        }
        case FieldCompression::CommonData: {
            uint32_t offset = additionalDataOffset(file.fieldStorageInfo, fieldIndex,
                                                     FieldCompression::CommonData);
            // Foreign, per-record exception list: {id, value} pairs, 8 bytes
            // each, only for rows that deviate from `defaultValue`. Reading
            // header.recordCount candidate rows here (not id -> row-count,
            // an unrelated file-level fact) is what actually bounds this
            // scan.
            uint32_t entryCount = info.additionalDataSize / 8;
            uint32_t rowId = rowIdForRecord(file, section, record, recordIndex);
            for (uint32_t i = 0; i < entryCount; ++i) {
                uint32_t entryPos = offset + i * 8;
                uint32_t id = readU32(file.commonData, entryPos, "commonData entry id");
                if (id == rowId) {
                    return {readU32(file.commonData, entryPos + 4, "commonData entry value")};
                }
            }
            return {info.defaultValue};
        }
        case FieldCompression::BitpackedIndexed: {
            uint64_t index = readBits(record, info.fieldOffsetBits, info.fieldSizeBits);
            uint32_t offset =
                additionalDataOffset(file.fieldStorageInfo, fieldIndex, FieldCompression::BitpackedIndexed);
            return {readPallet4(file.palletData, offset + static_cast<uint32_t>(index) * 4,
                                 "palletData indexed entry")};
        }
        case FieldCompression::BitpackedIndexedArray: {
            uint64_t index = readBits(record, info.fieldOffsetBits, info.fieldSizeBits);
            uint32_t offset = additionalDataOffset(file.fieldStorageInfo, fieldIndex,
                                                     FieldCompression::BitpackedIndexedArray);
            std::vector<uint64_t> values;
            values.reserve(info.arrayCount);
            for (uint32_t i = 0; i < info.arrayCount; ++i) {
                uint32_t entryOffset = offset + static_cast<uint32_t>(index) * 4 * info.arrayCount + i * 4;
                values.push_back(readPallet4(file.palletData, entryOffset, "palletData indexed-array entry"));
            }
            return values;
        }
    }
    throw ParseError("db2: unreachable field_compression value");
}

std::optional<std::string> resolveFieldString(const std::vector<uint8_t>& fileBytes,
                                                size_t fieldAbsoluteFilePos, uint64_t rawValue) {
    // Absolute file position of the referenced string, per DB2.md's WDC2+
    // rule ("the relative position from the beginning of the field where
    // this offset was stored to the position of the referenced string").
    // rawValue is attacker/format-controlled and added to a size_t position
    // below -- checked against fileBytes.size() before any read, same as
    // every other offset in this file; a value that overflows or lands
    // outside the buffer is "not a string", not a crash.
    if (rawValue > fileBytes.size()) return std::nullopt;
    size_t stringPos = fieldAbsoluteFilePos + rawValue;
    if (stringPos >= fileBytes.size()) return std::nullopt;

    constexpr size_t kMaxStringLen = 4096;
    size_t end = stringPos;
    size_t limit = std::min(fileBytes.size(), stringPos + kMaxStringLen);
    while (end < limit && fileBytes[end] != 0) {
        // Printable ASCII or UTF-8 continuation/lead bytes only -- control
        // characters (other than the NUL terminator itself) are the
        // heuristic's rejection signal for "this wasn't really a string
        // offset, just an integer that happened to land in-bounds".
        uint8_t c = fileBytes[end];
        if (c < 0x09 || (c >= 0x0e && c < 0x20)) return std::nullopt;
        ++end;
    }
    if (end == limit) return std::nullopt;  // no terminator found within the cap
    if (end == stringPos) return std::nullopt;  // empty string: not useful as a positive signal

    return std::string(reinterpret_cast<const char*>(fileBytes.data() + stringPos), end - stringPos);
}

}  // namespace husk::db2

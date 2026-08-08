#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

// WDC5 DB2 parser -- proof of concept, per documentation/wowdev-wiki/md/DB2.md
// (mirrored wowdev.wiki, "canonical" only in the sense of "best available
// community reference" -- it is reverse-engineered, not a Blizzard spec, and
// has been wrong before for other formats this project parses. The header/
// section-header/field_structure/field_storage_info layout below has been
// cross-checked byte-for-byte against a real file
// (/media/luna/data/wow_export/dbfilesclient/chrmodelmaterial.db2, WDC5,
// "WOWSTATIC_12_0_7_67808") and matches exactly; the per-field value-decode
// paths (bitpacked/common-data/pallet-indexed, multi-section string offsets)
// are implemented straight from the doc's pseudocode and are comparatively
// less exercised -- treat a decode that looks wrong as a reason to go back to
// the real bytes, not as ground truth on its own.
//
// Scope, deliberately: this is Stage 1 of TODO/CHAR_TEXTURE_COMPOSITING_TODO.md
// ("a new, real file-format parser ... scope the parser generally, even
// though Stage 2 only consumes a handful of specific tables"), built to let
// a human poke at real .db2 files (`husk db2-info`) before any table-specific
// consumer exists. What's NOT here yet, current-vs-target: WDB2..WDC4 (older
// container versions -- WDC5 only, since every file under
// /media/luna/data/wow_export/dbfilesclient/ checked so far is WDC5), the
// pre-WDC2/legacy string-block layout (unneeded -- WDC5 always carries the
// WDC2+ per-field string-offset scheme), and full decoding of offset-map
// ("sparse", flags & 0x01) sections -- those expose their raw variable-length
// record bytes but not per-field values, since the fixed-width field_offset_bits
// scheme below doesn't apply to them. WDC5 itself still carries no field
// *names* here -- no `field_structure`/`field_storage_info` entry in
// DB2.md's own struct definitions carries a name string anywhere (mis-cited
// in an earlier version of this comment as DB2.md's "Determining Field
// Types" section, which is actually about inferring a field's *type* --
// int/float/string -- not its name; that section says nothing about naming
// at all). Real column naming now lives one layer up: `dbd.hpp` parses
// WoWDBDefs' .dbd schema files (optional, local-only, see that file's own
// module comment) and `husk db2-export` (src/cmd_db2.cpp) uses it to
// produce a real, named, browsable SQLite database -- this file's own
// `File`/`decodeField` stay name-agnostic by design, exactly the "table-
// name-to-struct mapping" gap this comment used to describe as entirely
// unaddressed.
namespace husk::db2 {

struct ParseError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct Header {
    uint32_t versionNum = 0;  // 5 for WDC5, per DB2.md
    std::string schemaString;  // e.g. "WOWSTATIC_12_0_7_67808", NUL-trimmed
    uint32_t recordCount = 0;         // across all sections
    uint32_t fieldCount = 0;          // arrays count as 1 field
    uint32_t recordSize = 0;          // bytes, fixed-width records only
    uint32_t stringTableSize = 0;     // across all sections
    uint32_t tableHash = 0;
    uint32_t layoutHash = 0;
    uint32_t minId = 0;
    uint32_t maxId = 0;
    uint32_t locale = 0;
    uint16_t flags = 0;     // bit 0: has offset map (sparse/variable-length records)
    uint16_t idIndex = 0;   // which field carries the row ID, if flags & 0x04 == 0
    uint32_t totalFieldCount = 0;
    uint32_t bitpackedDataOffset = 0;
    uint32_t lookupColumnCount = 0;
    uint32_t fieldStorageInfoSize = 0;
    uint32_t commonDataSize = 0;
    uint32_t palletDataSize = 0;
    uint32_t sectionCount = 0;
};

struct SectionHeader {
    uint64_t tactKeyHash = 0;   // non-zero => this section's records are encrypted
    uint32_t fileOffset = 0;    // absolute offset of this section's records
    uint32_t recordCount = 0;
    uint32_t stringTableSize = 0;
    uint32_t offsetRecordsEnd = 0;  // only meaningful when flags & 0x01
    uint32_t idListSize = 0;        // bytes; idListSize/4 uint32 IDs
    uint32_t relationshipDataSize = 0;
    uint32_t offsetMapIdCount = 0;
    uint32_t copyTableCount = 0;
};

// field_structure, DB2.md: `size` is a *derived* byte width (32 - size) / 8,
// stored negated for historical reasons; `position` is the field's byte
// offset within a fixed-width record. Not used for offset-map records.
struct FieldStructure {
    int16_t size = 0;
    uint16_t position = 0;
};

// field_compression enum, DB2.md's WDC2+ section (WDC5 reuses it unchanged).
enum class FieldCompression : uint32_t {
    None = 0,
    Bitpacked = 1,
    CommonData = 2,
    BitpackedIndexed = 3,
    BitpackedIndexedArray = 4,
    BitpackedSigned = 5,
};

// field_storage_info, DB2.md's WDC2+ section. The three `extra*` words hold
// different data depending on `storageType` per DB2.md's tagged union --
// only `arrayCount` (BitpackedIndexedArray) and `signExtend`
// (Bitpacked/BitpackedSigned, flags bit 0x01) are surfaced by name; the rest
// are documented as "not useful for most purposes" and dropped here rather
// than carried as unread filler.
struct FieldStorageInfo {
    uint16_t fieldOffsetBits = 0;
    uint16_t fieldSizeBits = 0;      // sum of all array elements, in bits
    uint32_t additionalDataSize = 0; // bytes this field owns in common_data/pallet_data
    FieldCompression storageType = FieldCompression::None;
    uint32_t defaultValue = 0;  // CommonData only
    uint32_t arrayCount = 1;    // BitpackedIndexedArray only
    bool signExtend = false;    // Bitpacked/BitpackedSigned only
};

struct CopyTableEntry {
    uint32_t newId = 0;
    uint32_t copiedId = 0;
};

struct OffsetMapEntry {
    uint32_t offset = 0;
    uint16_t size = 0;
};

// relationship_entry, DB2.md's WDC2+ section (WDC5 reuses it unchanged) --
// replaces a column that was also used as a foreign-key lookup (e.g.
// SpellID in SpellX* tables) with a section-wide (foreignId -> record) map
// instead of storing it inline in every record.
struct RelationshipEntry {
    uint32_t foreignId = 0;
    // Normally the record's position within Section::recordBytes
    // (recordIndex * header.recordSize). DB2.md's WDC5 struct notes one
    // documented exception: "if flag 0x02 is set [...] relationship_entry
    // uses record IDs instead of record index" -- true for every real local
    // fixture found so far (the Collectable*Sparse tables, which are also
    // offset-map sections), so this field holds a real record ID rather than
    // an index whenever Header::flags & 0x02 is set. See Section::
    // hasRelationshipMap's own caller for which case applies.
    uint32_t recordIndexOrId = 0;
};

struct Section {
    SectionHeader header;
    // Fixed-width path (header.flags & 0x01 == 0): header.recordCount
    // records, each header.recordSize bytes, concatenated.
    std::vector<uint8_t> recordBytes;
    // Offset-map path (header.flags & 0x01 != 0): each record's raw bytes,
    // one entry per offsetMap entry with a non-zero offset -- see the module
    // doc comment's "Scope" note. Per-field decode is NOT implemented for
    // this path.
    std::vector<std::vector<uint8_t>> variableRecordBytes;
    std::vector<uint8_t> stringBlock;
    std::vector<uint32_t> idList;
    std::vector<CopyTableEntry> copyTable;
    std::vector<OffsetMapEntry> offsetMap;
    std::vector<uint32_t> offsetMapIdList;
    // relationship_mapping, decoded whenever header.relationshipDataSize > 0
    // -- min_id/max_id are the mapping's own declared ID range (not
    // necessarily equal to the file's header.minId/maxId).
    uint32_t relationshipMinId = 0;
    uint32_t relationshipMaxId = 0;
    std::vector<RelationshipEntry> relationshipEntries;

    bool hasOffsetMap() const { return header.tactKeyHash == 0 && !offsetMap.empty(); }
    bool hasRelationshipMap() const { return header.relationshipDataSize > 0; }
};

struct File {
    Header header;
    std::vector<FieldStructure> fieldStructures;
    std::vector<FieldStorageInfo> fieldStorageInfo;
    std::vector<uint8_t> palletData;
    std::vector<uint8_t> commonData;
    std::vector<Section> sections;
};

// Parses a complete WDC5 file already read into memory. Throws ParseError on
// a magic mismatch or any structural field (a *_size/*_count driving a later
// read) that runs past the end of the buffer -- foreign data, boundary-
// validated once here; nothing downstream re-checks bounds.
File parse(const std::vector<uint8_t>& fileBytes);

// Decodes field `fieldIndex` of the `recordIndex`-th fixed-width record in
// `section` (section.recordBytes, NOT the offset-map path) as raw unsigned
// values -- one per array element (storage type None only; every other
// compression type is scalar, so the result always has exactly 1 entry).
// Values are NOT interpreted as int/float/string; see resolveFieldString for
// the one heuristic this POC does apply. Throws ParseError if recordIndex/
// fieldIndex is out of range, or the field's declared bit range runs past
// the record.
std::vector<uint64_t> decodeField(const File& file, const Section& section, size_t recordIndex,
                                   size_t fieldIndex);

// The row's real ID, whichever of WDC5's two storage schemes this file
// uses: `section.idList[recordIndex]` when present (header.flags & 0x04,
// "has non-inline IDs" -- the ID occupies no field-array slot at all), else
// the value decoded straight out of the record at header.idIndex (the
// common case -- the ID is just another inline field). Falls back to
// `recordIndex` itself only if neither source is available (shouldn't
// happen on a real file, but never guessed at beyond that).
uint32_t recordId(const File& file, const Section& section, size_t recordIndex);

// Resolves a `$noninline,relation$` DBD column's real per-record value from
// `section.relationshipEntries` -- real for real tables like
// ChrModelTextureLayer, whose CharComponentTextureLayoutsID occupies no
// field-array slot at all under some layouts, only the section's own
// relationship_map (`recordIndexOrId` holding the record's own *position*
// in this case, verified against real `chrmodeltexturelayer.db2` data,
// header.flags & 0x02 clear). Returns one entry per record
// (`section.header.recordCount`), nullopt for a record with no matching
// map entry. Returns an all-empty vector, never a guess, when
// `header.flags & 0x02` is set -- DB2.md's own note that
// `relationship_entry` holds a real record *ID* rather than an index in
// that case means this function would need a real ID->position lookup
// (via `section.idList`, when present) to invert it correctly, and no real
// local fixture has exercised that combination for a *value* lookup (only
// for the ID-column case `recordId` already handles) to verify against.
std::vector<std::optional<uint32_t>> nonInlineRelationValuesByRecord(const File& file,
                                                                       const Section& section);

// Best-effort heuristic (DB2.md's "Determining Field Types": "every value in
// a string field will be an offset into the string block, which you can
// check") -- interprets `rawValue` as a WDC2+-style string offset relative
// to the start of the field it was read from (`fieldAbsoluteFilePos`, an
// absolute byte offset into the original file buffer) and returns the
// pointed-to string if that position holds a plausible (in-bounds,
// null-terminated within 4096 bytes, printable) C string. Returns nullopt
// otherwise -- callers should fall back to printing the raw integer, never
// assume a nullopt means "not a string" with certainty (this is a heuristic,
// not a schema).
std::optional<std::string> resolveFieldString(const std::vector<uint8_t>& fileBytes,
                                                size_t fieldAbsoluteFilePos, uint64_t rawValue);

}  // namespace husk::db2

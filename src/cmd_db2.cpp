#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "commands.hpp"
#include "db2.hpp"

// `husk db2-info` -- the WDC5 proof-of-concept's own `husk info` analogue
// (TODO/CHAR_TEXTURE_COMPOSITING_TODO.md Stage 1). Prints header/section/field
// structure unconditionally (cheap, always useful for "what table is this")
// and a sample of decoded rows on request -- see db2.hpp's module comment
// for what's out of scope (older container versions, offset-map per-field
// decode, table-name-to-struct mapping).
namespace husk::commands {

namespace {

void printUsage(std::ostream& out = std::cerr) {
    out << "usage: husk db2-info <file.db2> [--rows N]\n"
           "\n"
           "Parses a WDC5 DB2 file and prints its header, per-section layout,\n"
           "and per-field storage info. With --rows (default 5, 0 for none,\n"
           "'all' for every record), also dumps that many decoded rows from\n"
           "the first non-offset-map section -- raw values per field, with a\n"
           "best-effort string heuristic (see db2.hpp's resolveFieldString).\n"
           "\n"
           "Proof of concept: field names are not known (WDC5 carries no\n"
           "column names, only positions/sizes -- see db2.hpp), so fields are\n"
           "identified by index only.\n";
}

std::vector<uint8_t> readFileBytes(const std::string& path) {
    errno = 0;
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        throw db2::ParseError("couldn't open '" + path + "' for reading: " + std::strerror(errno));
    }
    errno = 0;
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (!f.good() && !f.eof()) {
        throw db2::ParseError("error reading '" + path + "': " + std::strerror(errno));
    }
    return bytes;
}

const char* compressionName(db2::FieldCompression c) {
    switch (c) {
        case db2::FieldCompression::None:
            return "none";
        case db2::FieldCompression::Bitpacked:
            return "bitpacked";
        case db2::FieldCompression::CommonData:
            return "common_data";
        case db2::FieldCompression::BitpackedIndexed:
            return "bitpacked_indexed";
        case db2::FieldCompression::BitpackedIndexedArray:
            return "bitpacked_indexed_array";
        case db2::FieldCompression::BitpackedSigned:
            return "bitpacked_signed";
    }
    return "unknown";
}

void printHeader(const db2::Header& h) {
    std::cout << "  schema: " << h.schemaString << " (WDC5 version " << h.versionNum << ")\n";
    std::cout << "  record_count: " << h.recordCount << "   field_count: " << h.fieldCount
               << " (total " << h.totalFieldCount << ")\n";
    std::cout << "  record_size: " << h.recordSize << " bytes   string_table_size: "
               << h.stringTableSize << " bytes\n";
    std::cout << "  table_hash: 0x" << std::hex << h.tableHash << "   layout_hash: 0x"
               << h.layoutHash << std::dec << "\n";
    std::cout << "  id range: [" << h.minId << ", " << h.maxId << "]   id_index: " << h.idIndex
               << "\n";
    std::cout << "  flags: 0x" << std::hex << h.flags << std::dec << " (";
    bool any = false;
    auto flag = [&](uint16_t bit, const char* name) {
        if (h.flags & bit) {
            if (any) std::cout << ", ";
            std::cout << name;
            any = true;
        }
    };
    flag(0x01, "has offset map");
    flag(0x02, "has relationship data");
    flag(0x04, "has non-inline IDs");
    flag(0x10, "is bitpacked");
    if (!any) std::cout << "none set";
    std::cout << ")\n";
    std::cout << "  common_data: " << h.commonDataSize << " bytes   pallet_data: "
               << h.palletDataSize << " bytes\n";
    std::cout << "  sections: " << h.sectionCount << "\n";
}

void printSections(const db2::File& file) {
    for (size_t i = 0; i < file.sections.size(); ++i) {
        const db2::Section& s = file.sections[i];
        std::cout << "  section " << i << ": " << s.header.recordCount << " records at file offset 0x"
                   << std::hex << s.header.fileOffset << std::dec;
        if (s.header.tactKeyHash != 0) {
            std::cout << "  [ENCRYPTED, tact_key_hash=0x" << std::hex << s.header.tactKeyHash
                       << std::dec << ", record bytes unreadable without the TACT key]";
        }
        if (!s.offsetMap.empty()) {
            std::cout << "  (offset-map/sparse -- per-field decode not implemented, see --rows output)";
        }
        std::cout << "\n";
    }
}

void printFields(const db2::File& file) {
    std::cout << "  fields (" << file.fieldStorageInfo.size() << "):\n";
    for (size_t i = 0; i < file.fieldStorageInfo.size(); ++i) {
        const db2::FieldStorageInfo& info = file.fieldStorageInfo[i];
        std::cout << "    [" << i << "] " << compressionName(info.storageType) << "  offset_bits="
                   << info.fieldOffsetBits << " size_bits=" << info.fieldSizeBits;
        if (info.storageType == db2::FieldCompression::CommonData) {
            std::cout << " default=" << info.defaultValue;
        }
        if (info.storageType == db2::FieldCompression::BitpackedIndexedArray) {
            std::cout << " array_count=" << info.arrayCount;
        }
        if (i == file.header.idIndex && !(file.header.flags & 0x04)) {
            std::cout << "  (id field)";
        }
        std::cout << "\n";
    }
}

// Prints up to `rowLimit` decoded rows from the first section that has
// fixed-width records (offset-map sections are skipped -- see db2.hpp's
// module comment on what this POC does and doesn't decode). `rowLimit ==
// SIZE_MAX` means "all". `fileBytes` is the original file buffer, needed by
// the string heuristic (db2::resolveFieldString resolves an absolute file
// position, not a section-relative one).
void printRows(const db2::File& file, const std::vector<uint8_t>& fileBytes, size_t rowLimit) {
    const db2::Section* section = nullptr;
    for (const db2::Section& s : file.sections) {
        if (s.header.tactKeyHash != 0) continue;  // encrypted, unreadable
        if (!s.offsetMap.empty()) continue;        // offset-map path, no per-field decode
        section = &s;
        break;
    }
    if (!section) {
        std::cout << "  (no section with decodable fixed-width records -- every section is "
                     "encrypted and/or offset-map/sparse)\n";
        return;
    }

    size_t n = std::min(static_cast<size_t>(section->header.recordCount), rowLimit);
    std::cout << "  rows (" << n << " of " << section->header.recordCount << "):\n";
    for (size_t r = 0; r < n; ++r) {
        std::cout << "    row " << r << ":";
        for (size_t f = 0; f < file.fieldStorageInfo.size(); ++f) {
            std::vector<uint64_t> values = db2::decodeField(file, *section, r, f);
            std::cout << " [" << f << "]=";
            bool isScalarNone =
                values.size() == 1 && file.fieldStorageInfo[f].storageType == db2::FieldCompression::None;
            std::optional<std::string> str;
            if (isScalarNone) {
                size_t fieldAbsPos =
                    section->header.fileOffset + r * file.header.recordSize + file.fieldStructures[f].position;
                str = db2::resolveFieldString(fileBytes, fieldAbsPos, values[0]);
            }
            if (str) {
                std::cout << "\"" << *str << "\"";
            } else if (values.size() > 1) {
                std::cout << "{";
                for (size_t i = 0; i < values.size(); ++i) {
                    if (i) std::cout << ",";
                    std::cout << values[i];
                }
                std::cout << "}";
            } else {
                std::cout << values[0];
            }
        }
        std::cout << "\n";
    }
}

}  // namespace

int db2Info(int argc, char** args) {
    if (argc >= 1 && isHelpFlag(args[0])) {
        printUsage(std::cout);
        return 0;
    }
    if (argc < 1 || argc > 3) {
        printUsage();
        return 1;
    }

    std::string path = args[0];
    size_t rowLimit = 5;
    if (argc == 3) {
        std::string flag = args[1];
        std::string value = args[2];
        if (flag != "--rows") {
            printUsage();
            return 1;
        }
        if (value == "all") {
            rowLimit = static_cast<size_t>(-1);
        } else {
            try {
                rowLimit = static_cast<size_t>(std::stoul(value));
            } catch (const std::exception&) {
                std::cerr << "husk: db2-info: --rows expects a non-negative integer or 'all', got '"
                          << value << "'\n";
                return 1;
            }
        }
    } else if (argc == 2) {
        printUsage();
        return 1;
    }

    db2::File file;
    std::vector<uint8_t> bytes;
    try {
        bytes = readFileBytes(path);
        file = db2::parse(bytes);
    } catch (const std::exception& e) {
        std::cerr << "husk: couldn't read '" << path << "': " << e.what() << "\n";
        return 1;
    }

    std::cout << path << "\n";
    printHeader(file.header);
    printSections(file);
    printFields(file);
    if (rowLimit > 0) {
        printRows(file, bytes, rowLimit);
    }

    return 0;
}

}  // namespace husk::commands

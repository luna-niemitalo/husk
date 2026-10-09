#pragma once

#include <cstdint>
#include <string>
#include <variant>

// husk::canon: see canon_policy.hpp for the layer this belongs to.
// This file adds canon::Ref, the identity+name carrier CANONICAL_MODEL.md
// specifies -- still a pure value type, no consumers wired to it yet.
namespace husk::canon {

// Where a Ref's name string came from -- lets a consumer judge how much to
// trust it, since a synthesized placeholder and a real DB2 fact currently
// arrive downstream as identical std::strings with no way to tell apart.
enum class NameSource {
    M2Embedded,
    // A file path stored verbatim in an ADT name table (MMDX/MWMO/MTEX) --
    // the pre-FileDataID way a tile names what it places. Only ever carried
    // by a Ref whose path the listfile could not turn into a FileDataID.
    AdtEmbedded,
    Listfile,
    Db2,
    Synthesized,
    None,
};

// No real identity exists to fall back on. Distinct from NameSource::None:
// that describes an absent *name*, this describes an absent *identity* --
// a Ref can have one without the other in either direction. Listed first
// so a default-constructed Identity is None, not an arbitrary zero-valued
// FileDataId.
struct None {};

// The four identity kinds a thing in this codebase can carry. Different
// kinds of things have different real identities -- a texture has a
// FileDataID, a bone does not -- so this is a tagged union, not one
// integer field reused for everything.
struct FileDataId {
    uint32_t value;
};

// table + row, the same two facts every db2table.hpp/cmd_db2.cpp lookup
// already keys a row by -- there's no existing single struct bundling
// them, so this is the first one.
struct Db2Row {
    std::string table;
    uint32_t row;
};

// Plain array position -- "this is bone #7", with no FileDataID or DB2 row
// behind it at all.
struct RecordIndex {
    uint32_t value;
};

// std::monostate would also default-construct to the first alternative,
// but a named None reads at a holds_alternative<None> call site instead
// of a bare std::monostate.
using Identity = std::variant<None, FileDataId, Db2Row, RecordIndex>;

// The I6 carrier: real identity next to the decorative name, so a
// consumer can always fall back to the identity it can actually trust
// instead of the name alone.
struct Ref {
    Identity id;
    std::string name;
    NameSource source = NameSource::None;
};

}  // namespace husk::canon

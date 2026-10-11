#pragma once

#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <ostream>
#include <string>
#include <vector>

// A minimal, purpose-built streaming JSON writer -- no DOM, no external
// dependency. Written for husk's own "dump chunk data to a readable
// intermediary format" need (see cmd_dump.cpp): only what that actually
// uses (objects, arrays, strings, integers, doubles, bools) is
// implemented, nothing else -- this is not meant to become a general-
// purpose JSON library.
namespace husk::json {

class Writer {
public:
    explicit Writer(std::ostream& out) : out_(out) {}

    void beginObject() { open('{'); }
    void endObject() { close('}'); }
    void beginArray() { open('['); }
    void endArray() { close(']'); }

    void key(const std::string& k) {
        comma();
        indent();
        out_ << '"' << escape(k) << "\": ";
        pending_ = true;
    }

    void value(const std::string& s) {
        prefix();
        out_ << '"' << escape(s) << '"';
        pending_ = false;
    }
    void value(const char* s) { value(std::string(s)); }
    void value(double v) {
        prefix();
        // JSON has no NaN/Infinity literal -- emit null and let the
        // reader notice, rather than writing text that isn't valid JSON.
        if (!std::isfinite(v)) {
            out_ << "null";
            pending_ = false;
            return;
        }
        // The shortest text that reads back exactly (`<<` kept 6 digits,
        // 0.03 yd off at world coordinates). A double holding a float value
        // is written as that float -- 17066.666, not 17066.666015625.
        char text[32];
        float asFloat = static_cast<float>(v);
        std::to_chars_result written = static_cast<double>(asFloat) == v
                                           ? std::to_chars(text, text + sizeof(text), asFloat)
                                           : std::to_chars(text, text + sizeof(text), v);
        out_.write(text, written.ptr - text);
        pending_ = false;
    }
    void value(int64_t v) {
        prefix();
        out_ << v;
        pending_ = false;
    }
    void value(bool b) {
        prefix();
        out_ << (b ? "true" : "false");
        pending_ = false;
    }
    void nullValue() {
        prefix();
        out_ << "null";
        pending_ = false;
    }

    // Splices the members of a JSON object given as text into the open
    // object: for documents whose body is a checked-in file (effects/),
    // so the writer adds only the header and never re-encodes the body.
    void rawMembers(const std::string& objectText) {
        size_t open = objectText.find('{');
        size_t close = objectText.rfind('}');
        std::string inner = objectText.substr(open + 1, close - open - 1);
        if (inner.find_first_not_of(" \t\r\n") == std::string::npos) return;
        comma();
        out_ << inner.substr(inner.find_first_not_of(" \t\r\n"));
    }

private:
    std::ostream& out_;
    std::vector<bool> firstInScope_;  // one entry per open object/array
    bool pending_ = false;            // true right after key(), before its value

    void prefix() {
        if (!pending_) {
            comma();
            indent();
        }
    }
    void open(char c) {
        prefix();
        out_ << c << '\n';
        firstInScope_.push_back(true);
        pending_ = false;
    }
    void close(char c) {
        bool wasEmpty = firstInScope_.back();
        firstInScope_.pop_back();
        if (!wasEmpty) out_ << '\n';
        indent();
        out_ << c;
    }
    void comma() {
        if (firstInScope_.empty()) return;
        if (!firstInScope_.back()) {
            out_ << ",\n";
        }
        firstInScope_.back() = false;
    }
    void indent() {
        for (size_t i = 0; i < firstInScope_.size(); ++i) out_ << "  ";
    }
    static std::string escape(const std::string& s) {
        std::string out;
        out.reserve(s.size());
        for (char c : s) {
            switch (c) {
                case '"': out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default:
                    if (static_cast<unsigned char>(c) < 0x20) {
                        char buf[8];
                        std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                        out += buf;
                    } else {
                        out += c;
                    }
            }
        }
        return out;
    }
};

}  // namespace husk::json

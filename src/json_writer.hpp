#pragma once

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
        if (std::isfinite(v)) {
            out_ << v;
        } else {
            out_ << "null";
        }
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

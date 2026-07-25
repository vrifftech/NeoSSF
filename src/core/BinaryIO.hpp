#pragma once

#include "core/CoreTypes.hpp"

#include <cstring>
#include <istream>
#include <ostream>
#include <stdexcept>
#include <string>

namespace neossf::binary {

inline void readExact(std::istream& in, char* buffer, std::size_t size, const std::string& what) {
    in.read(buffer, static_cast<std::streamsize>(size));
    if (!in) {
        throw std::runtime_error("Unable to read " + what + ".");
    }
}

inline void writeExact(std::ostream& out, const char* buffer, std::size_t size, const std::string& what) {
    out.write(buffer, static_cast<std::streamsize>(size));
    if (!out) {
        throw std::runtime_error("Unable to write " + what + ".");
    }
}

inline FourCC readFourCC(std::istream& in, const std::string& what) {
    FourCC result{};
    readExact(in, result.data(), result.size(), what);
    return result;
}

inline void writeFourCC(std::ostream& out, const FourCC& value, const std::string& what) {
    writeExact(out, value.data(), value.size(), what);
}

inline UInt32 readU32LE(std::istream& in, const std::string& what) {
    unsigned char b[4]{};
    readExact(in, reinterpret_cast<char*>(b), sizeof(b), what);
    return static_cast<UInt32>(b[0]) |
           (static_cast<UInt32>(b[1]) << 8u) |
           (static_cast<UInt32>(b[2]) << 16u) |
           (static_cast<UInt32>(b[3]) << 24u);
}

inline void writeU32LE(std::ostream& out, UInt32 value, const std::string& what) {
    unsigned char b[4] = {
        static_cast<unsigned char>(value & 0xFFu),
        static_cast<unsigned char>((value >> 8u) & 0xFFu),
        static_cast<unsigned char>((value >> 16u) & 0xFFu),
        static_cast<unsigned char>((value >> 24u) & 0xFFu),
    };
    writeExact(out, reinterpret_cast<const char*>(b), sizeof(b), what);
}

inline float readFloatLE(std::istream& in, const std::string& what) {
    const UInt32 raw = readU32LE(in, what);
    float result = 0.0f;
    static_assert(sizeof(result) == sizeof(raw), "Single-precision float must be 32 bits");
    std::memcpy(&result, &raw, sizeof(result));
    return result;
}

inline void writeFloatLE(std::ostream& out, float value, const std::string& what) {
    UInt32 raw = 0;
    static_assert(sizeof(value) == sizeof(raw), "Single-precision float must be 32 bits");
    std::memcpy(&raw, &value, sizeof(raw));
    writeU32LE(out, raw, what);
}

} // namespace neossf::binary

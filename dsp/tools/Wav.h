// Minimal WAV reader/writer for the PC tools (PCM 16/24/32, float32,
// WAVE_FORMAT_EXTENSIBLE with channel mask).
#pragma once

#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace wav {

struct Audio {
    int channels = 0;
    int sampleRate = 0;
    uint32_t channelMask = 0;  // 0 when the file carries none
    std::vector<float> samples;  // interleaved

    size_t frames() const { return channels ? samples.size() / channels : 0; }
};

namespace detail {
inline uint16_t u16(const unsigned char* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
inline uint32_t u32(const unsigned char* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
inline void put16(std::ofstream& f, uint16_t v) {
    const unsigned char b[2] = {static_cast<unsigned char>(v), static_cast<unsigned char>(v >> 8)};
    f.write(reinterpret_cast<const char*>(b), 2);
}
inline void put32(std::ofstream& f, uint32_t v) {
    const unsigned char b[4] = {static_cast<unsigned char>(v), static_cast<unsigned char>(v >> 8),
                                static_cast<unsigned char>(v >> 16),
                                static_cast<unsigned char>(v >> 24)};
    f.write(reinterpret_cast<const char*>(b), 4);
}
}  // namespace detail

inline bool read(const std::string& path, Audio* out, std::string* error) {
    using namespace detail;
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        *error = "cannot open " + path;
        return false;
    }
    std::vector<unsigned char> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (d.size() < 12 || std::memcmp(d.data(), "RIFF", 4) != 0 || std::memcmp(d.data() + 8, "WAVE", 4) != 0) {
        *error = "not a RIFF/WAVE file";
        return false;
    }
    int format = 0, bits = 0;
    const unsigned char* data = nullptr;
    size_t dataSize = 0;
    for (size_t pos = 12; pos + 8 <= d.size();) {
        const unsigned char* c = d.data() + pos;
        const uint32_t size = u32(c + 4);
        const size_t avail = d.size() - pos - 8;
        const size_t len = size > avail ? avail : size;
        if (std::memcmp(c, "fmt ", 4) == 0 && len >= 16) {
            format = u16(c + 8);
            out->channels = u16(c + 10);
            out->sampleRate = static_cast<int>(u32(c + 12));
            bits = u16(c + 22);
            if (format == 0xFFFE && len >= 40) {
                out->channelMask = u32(c + 28);
                format = u16(c + 32);  // first two bytes of the sub-format GUID
            }
        } else if (std::memcmp(c, "data", 4) == 0) {
            data = c + 8;
            dataSize = len;
        }
        pos += 8 + size + (size & 1);
    }
    if (data == nullptr || out->channels <= 0) {
        *error = "missing fmt or data chunk";
        return false;
    }
    const int bytes = bits / 8;
    if (!((format == 1 && (bits == 16 || bits == 24 || bits == 32)) || (format == 3 && bits == 32))) {
        *error = "unsupported sample format (format " + std::to_string(format) + ", " +
                 std::to_string(bits) + " bit)";
        return false;
    }
    const size_t count = dataSize / bytes;
    out->samples.resize(count - count % out->channels);
    for (size_t i = 0; i < out->samples.size(); ++i) {
        const unsigned char* s = data + i * bytes;
        float v = 0.0f;
        if (format == 3) {
            std::memcpy(&v, s, 4);
        } else if (bits == 16) {
            v = static_cast<int16_t>(u16(s)) / 32768.0f;
        } else if (bits == 24) {
            const uint32_t packed = (static_cast<uint32_t>(s[0]) << 8) |
                                    (static_cast<uint32_t>(s[1]) << 16) |
                                    (static_cast<uint32_t>(s[2]) << 24);
            const int32_t x = static_cast<int32_t>(packed) >> 8;
            v = x / 8388608.0f;
        } else {
            v = static_cast<int32_t>(u32(s)) / 2147483648.0f;
        }
        out->samples[i] = v;
    }
    return true;
}

// Writes 32-bit float; uses WAVE_FORMAT_EXTENSIBLE when a mask is given.
inline bool writeFloat(const std::string& path, const Audio& a, std::string* error) {
    using namespace detail;
    std::ofstream f(path, std::ios::binary);
    if (!f) {
        *error = "cannot create " + path;
        return false;
    }
    const bool ext = a.channelMask != 0;
    const uint32_t fmtSize = ext ? 40 : 16;
    const uint32_t dataSize = static_cast<uint32_t>(a.samples.size() * 4);
    f.write("RIFF", 4);
    put32(f, 4 + 8 + fmtSize + 8 + dataSize);
    f.write("WAVE", 4);
    f.write("fmt ", 4);
    put32(f, fmtSize);
    put16(f, ext ? 0xFFFE : 3);
    put16(f, static_cast<uint16_t>(a.channels));
    put32(f, static_cast<uint32_t>(a.sampleRate));
    put32(f, static_cast<uint32_t>(a.sampleRate * a.channels * 4));
    put16(f, static_cast<uint16_t>(a.channels * 4));
    put16(f, 32);
    if (ext) {
        put16(f, 22);
        put16(f, 32);
        put32(f, a.channelMask);
        // KSDATAFORMAT_SUBTYPE_IEEE_FLOAT
        const unsigned char guid[16] = {0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00,
                                        0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71};
        f.write(reinterpret_cast<const char*>(guid), 16);
    }
    f.write("data", 4);
    put32(f, dataSize);
    f.write(reinterpret_cast<const char*>(a.samples.data()), dataSize);
    return static_cast<bool>(f);
}

}  // namespace wav

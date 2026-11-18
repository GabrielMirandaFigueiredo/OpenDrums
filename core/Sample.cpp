#include "Sample.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>

namespace od {

namespace {

uint16_t rd16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t rd32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

void wr16(std::ofstream& f, uint16_t v) {
    char b[2] = {static_cast<char>(v & 0xFF), static_cast<char>((v >> 8) & 0xFF)};
    f.write(b, 2);
}
void wr32(std::ofstream& f, uint32_t v) {
    char b[4] = {static_cast<char>(v & 0xFF), static_cast<char>((v >> 8) & 0xFF),
                 static_cast<char>((v >> 16) & 0xFF), static_cast<char>((v >> 24) & 0xFF)};
    f.write(b, 4);
}

} // namespace

std::shared_ptr<const Sample> loadWav(const std::filesystem::path& file, std::string& error) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        error = "cannot open " + file.string();
        return nullptr;
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (bytes.size() < 12 || std::memcmp(bytes.data(), "RIFF", 4) != 0 ||
        std::memcmp(bytes.data() + 8, "WAVE", 4) != 0) {
        error = "not a RIFF/WAVE file: " + file.string();
        return nullptr;
    }

    uint16_t format = 0, channels = 0, bits = 0;
    uint32_t rate = 0;
    const uint8_t* data = nullptr;
    size_t dataSize = 0;

    size_t pos = 12;
    while (pos + 8 <= bytes.size()) {
        const uint8_t* ck = bytes.data() + pos;
        const uint32_t size = rd32(ck + 4);
        const size_t body = pos + 8;
        const size_t avail = bytes.size() - body;
        const size_t len = std::min<size_t>(size, avail);

        if (std::memcmp(ck, "fmt ", 4) == 0 && len >= 16) {
            format = rd16(bytes.data() + body);
            channels = rd16(bytes.data() + body + 2);
            rate = rd32(bytes.data() + body + 4);
            bits = rd16(bytes.data() + body + 14);
            if (format == 0xFFFE && len >= 26) // WAVE_FORMAT_EXTENSIBLE
                format = rd16(bytes.data() + body + 24);
        } else if (std::memcmp(ck, "data", 4) == 0) {
            data = bytes.data() + body;
            dataSize = len;
        }
        pos = body + len + (len & 1); // chunks are word aligned
    }

    if (!data || channels == 0 || rate == 0) {
        error = "missing fmt/data chunk: " + file.string();
        return nullptr;
    }
    const bool isFloat = (format == 3);
    if (!(format == 1 || format == 3) || (isFloat && bits != 32) ||
        (!isFloat && bits != 8 && bits != 16 && bits != 24 && bits != 32)) {
        error = "unsupported WAV encoding (" + std::to_string(bits) + " bit, format " +
                std::to_string(format) + "): " + file.string();
        return nullptr;
    }

    const size_t bytesPerSample = bits / 8;
    const size_t frameSize = bytesPerSample * channels;
    const size_t frames = dataSize / frameSize;

    auto sample = std::make_shared<Sample>();
    sample->sampleRate = rate;
    sample->left.resize(frames);
    if (channels >= 2) sample->right.resize(frames);

    auto readOne = [&](const uint8_t* p) -> float {
        if (isFloat) {
            float f;
            std::memcpy(&f, p, 4);
            return f;
        }
        switch (bits) {
        case 8: return (static_cast<int>(p[0]) - 128) / 128.0f;
        case 16: return static_cast<int16_t>(rd16(p)) / 32768.0f;
        case 24: {
            int32_t v = (p[0] << 8) | (p[1] << 16) | (static_cast<int32_t>(p[2]) << 24);
            return static_cast<float>(v >> 8) / 8388608.0f;
        }
        default: return static_cast<int32_t>(rd32(p)) / 2147483648.0f;
        }
    };

    for (size_t i = 0; i < frames; ++i) {
        const uint8_t* fp = data + i * frameSize;
        sample->left[i] = readOne(fp);
        if (channels >= 2) sample->right[i] = readOne(fp + bytesPerSample);
    }
    return sample;
}

bool writeWav16(const std::filesystem::path& file, const std::vector<float>& left,
                const std::vector<float>& right, double sampleRate, std::string& error) {
    std::ofstream out(file, std::ios::binary);
    if (!out) {
        error = "cannot write " + file.string();
        return false;
    }
    const uint32_t frames = static_cast<uint32_t>(left.size());
    const uint32_t dataBytes = frames * 4;
    out.write("RIFF", 4);
    wr32(out, 36 + dataBytes);
    out.write("WAVEfmt ", 8);
    wr32(out, 16);
    wr16(out, 1);
    wr16(out, 2);
    wr32(out, static_cast<uint32_t>(sampleRate));
    wr32(out, static_cast<uint32_t>(sampleRate) * 4);
    wr16(out, 4);
    wr16(out, 16);
    out.write("data", 4);
    wr32(out, dataBytes);
    for (uint32_t i = 0; i < frames; ++i) {
        const float l = std::clamp(left[i], -1.0f, 1.0f);
        const float r = std::clamp(i < right.size() ? right[i] : left[i], -1.0f, 1.0f);
        wr16(out, static_cast<uint16_t>(static_cast<int16_t>(std::lrint(l * 32767.0f))));
        wr16(out, static_cast<uint16_t>(static_cast<int16_t>(std::lrint(r * 32767.0f))));
    }
    return static_cast<bool>(out);
}

} // namespace od

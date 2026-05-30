#include "image_utils.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

namespace {

void appendU32(std::vector<unsigned char>& out, std::uint32_t value) {
    out.push_back(static_cast<unsigned char>((value >> 24) & 0xff));
    out.push_back(static_cast<unsigned char>((value >> 16) & 0xff));
    out.push_back(static_cast<unsigned char>((value >> 8) & 0xff));
    out.push_back(static_cast<unsigned char>(value & 0xff));
}

std::uint32_t crc32(const unsigned char* data, std::size_t size) {
    std::uint32_t crc = 0xffffffffu;
    for (std::size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
        }
    }
    return crc ^ 0xffffffffu;
}

std::uint32_t adler32(const std::vector<unsigned char>& data) {
    constexpr std::uint32_t mod = 65521u;
    std::uint32_t a = 1u;
    std::uint32_t b = 0u;
    for (const unsigned char value : data) {
        a = (a + value) % mod;
        b = (b + a) % mod;
    }
    return (b << 16) | a;
}

void addChunk(std::vector<unsigned char>& png, const char type[4], const std::vector<unsigned char>& data) {
    appendU32(png, static_cast<std::uint32_t>(data.size()));
    const std::size_t typeOffset = png.size();
    png.insert(png.end(), type, type + 4);
    png.insert(png.end(), data.begin(), data.end());
    appendU32(png, crc32(png.data() + typeOffset, 4 + data.size()));
}

std::vector<unsigned char> zlibStore(const std::vector<unsigned char>& raw) {
    std::vector<unsigned char> out;
    out.push_back(0x78);
    out.push_back(0x01);

    std::size_t offset = 0;
    while (offset < raw.size()) {
        const std::size_t blockSize = std::min<std::size_t>(65535, raw.size() - offset);
        const bool finalBlock = offset + blockSize == raw.size();
        out.push_back(finalBlock ? 0x01 : 0x00);

        const auto len = static_cast<std::uint16_t>(blockSize);
        const auto nlen = static_cast<std::uint16_t>(~len);
        out.push_back(static_cast<unsigned char>(len & 0xff));
        out.push_back(static_cast<unsigned char>((len >> 8) & 0xff));
        out.push_back(static_cast<unsigned char>(nlen & 0xff));
        out.push_back(static_cast<unsigned char>((nlen >> 8) & 0xff));
        out.insert(out.end(), raw.begin() + static_cast<std::ptrdiff_t>(offset),
                   raw.begin() + static_cast<std::ptrdiff_t>(offset + blockSize));
        offset += blockSize;
    }

    appendU32(out, adler32(raw));
    return out;
}

std::vector<unsigned char> encodePngGray(const std::vector<unsigned char>& pixels, int width, int height) {
    std::vector<unsigned char> png = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};

    std::vector<unsigned char> ihdr;
    appendU32(ihdr, static_cast<std::uint32_t>(width));
    appendU32(ihdr, static_cast<std::uint32_t>(height));
    ihdr.push_back(8);
    ihdr.push_back(0);
    ihdr.push_back(0);
    ihdr.push_back(0);
    ihdr.push_back(0);
    addChunk(png, "IHDR", ihdr);

    std::vector<unsigned char> raw;
    raw.reserve(static_cast<std::size_t>((width + 1) * height));
    for (int y = 0; y < height; ++y) {
        raw.push_back(0);
        const std::size_t start = static_cast<std::size_t>(y * width);
        raw.insert(raw.end(), pixels.begin() + static_cast<std::ptrdiff_t>(start),
                   pixels.begin() + static_cast<std::ptrdiff_t>(start + width));
    }

    addChunk(png, "IDAT", zlibStore(raw));
    addChunk(png, "IEND", {});
    return png;
}

} // namespace

bool saveImageFromVector(const std::vector<double>& values, const std::string& path, int sizePixels) {
    if (values.empty() || sizePixels <= 0) {
        return false;
    }

    int width = static_cast<int>(std::sqrt(static_cast<double>(sizePixels)));
    int height = width;
    if (width * width != sizePixels) {
        width = sizePixels;
        height = 1;
    }

    std::vector<unsigned char> pixels(static_cast<std::size_t>(width * height), 0);
    const auto [minIt, maxIt] = std::minmax_element(values.begin(), values.end());
    double minValue = *minIt;
    double maxValue = *maxIt;
    if (minValue == maxValue) {
        minValue = maxValue - 1.0;
    }

    const std::size_t count = std::min<std::size_t>(pixels.size(), values.size());
    for (std::size_t i = 0; i < count; ++i) {
        const double norm = (values[i] - minValue) / (maxValue - minValue);
        if (norm <= 0.0) {
            pixels[i] = 0;
        } else if (norm >= 1.0) {
            pixels[i] = 255;
        } else {
            pixels[i] = static_cast<unsigned char>(norm * 255.0);
        }
    }

    const std::vector<unsigned char> png = encodePngGray(pixels, width, height);
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        return false;
    }
    out.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
    return static_cast<bool>(out);
}

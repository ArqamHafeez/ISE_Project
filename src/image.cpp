#include "image.h"

#include <cstring>
#include <fstream>
#include <limits>
#include <utility>

using namespace std;

namespace {

constexpr uint64_t kMaximumPixels = 40'000'000;

uint16_t readU16(const vector<uint8_t>& bytes, size_t offset) {
    return static_cast<uint16_t>(bytes[offset]) |
           (static_cast<uint16_t>(bytes[offset + 1]) << 8);
}

uint32_t readU32(const vector<uint8_t>& bytes, size_t offset) {
    return static_cast<uint32_t>(bytes[offset]) |
           (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
           (static_cast<uint32_t>(bytes[offset + 2]) << 16) |
           (static_cast<uint32_t>(bytes[offset + 3]) << 24);
}

int32_t readI32(const vector<uint8_t>& bytes, size_t offset) {
    const uint32_t value = readU32(bytes, offset);
    int32_t result;
    static_assert(sizeof(result) == sizeof(value), "32-bit integer size is required");
    memcpy(&result, &value, sizeof(result));
    return result;
}

void writeU16(ofstream& output, uint16_t value) {
    const char bytes[] = {
        static_cast<char>(value & 0xff),
        static_cast<char>((value >> 8) & 0xff)
    };
    output.write(bytes, sizeof(bytes));
}

void writeU32(ofstream& output, uint32_t value) {
    const char bytes[] = {
        static_cast<char>(value & 0xff),
        static_cast<char>((value >> 8) & 0xff),
        static_cast<char>((value >> 16) & 0xff),
        static_cast<char>((value >> 24) & 0xff)
    };
    output.write(bytes, sizeof(bytes));
}

}  // namespace

bool loadBmp(const string& path, Image& image, string& error) {
    ifstream input(path, ios::binary | ios::ate);
    if (!input) {
        error = "Could not open the image file.";
        return false;
    }

    const streamoff fileSize = input.tellg();
    if (fileSize < 54 || fileSize > 256 * 1024 * 1024) {
        error = "The file is too small or too large to be a supported BMP image.";
        return false;
    }

    vector<uint8_t> bytes(static_cast<size_t>(fileSize));
    input.seekg(0, ios::beg);
    if (!input.read(reinterpret_cast<char*>(bytes.data()), fileSize)) {
        error = "Could not read the image file.";
        return false;
    }

    if (bytes[0] != 'B' || bytes[1] != 'M') {
        error = "Unsupported image format. Please use a BMP image.";
        return false;
    }

    const uint32_t pixelOffset = readU32(bytes, 10);
    const uint32_t headerSize = readU32(bytes, 14);
    if (headerSize < 40 || static_cast<uint64_t>(14) + headerSize > bytes.size()) {
        error = "The BMP header is invalid or unsupported.";
        return false;
    }

    const int32_t signedWidth = readI32(bytes, 18);
    const int32_t signedHeight = readI32(bytes, 22);
    const uint16_t planes = readU16(bytes, 26);
    const uint16_t bitsPerPixel = readU16(bytes, 28);
    const uint32_t compression = readU32(bytes, 30);

    if (signedWidth <= 0 || signedHeight == 0 ||
        signedHeight == numeric_limits<int32_t>::min() ||
        planes != 1 || compression != 0 ||
        (bitsPerPixel != 24 && bitsPerPixel != 32)) {
        error = "Please use an uncompressed 24-bit or 32-bit BMP image.";
        return false;
    }

    const uint32_t width = static_cast<uint32_t>(signedWidth);
    const uint32_t height = static_cast<uint32_t>(
        signedHeight < 0 ? -signedHeight : signedHeight);
    if (static_cast<uint64_t>(width) * height > kMaximumPixels) {
        error = "The BMP image dimensions are too large.";
        return false;
    }

    const uint64_t rowSize =
        ((static_cast<uint64_t>(width) * bitsPerPixel + 31) / 32) * 4;
    const uint64_t pixelBytes = rowSize * height;
    if (pixelOffset < 14 + headerSize ||
        static_cast<uint64_t>(pixelOffset) + pixelBytes > bytes.size()) {
        error = "The BMP pixel data is incomplete or invalid.";
        return false;
    }

    Image loaded;
    loaded.width = width;
    loaded.height = height;
    loaded.pixels.resize(static_cast<size_t>(width) * height * 3);

    const size_t bytesPerPixel = bitsPerPixel / 8;
    for (uint32_t y = 0; y < height; ++y) {
        const uint32_t sourceY = signedHeight > 0 ? height - 1 - y : y;
        const size_t sourceRow =
            static_cast<size_t>(pixelOffset + rowSize * sourceY);
        for (uint32_t x = 0; x < width; ++x) {
            const size_t sourceIndex = sourceRow + x * bytesPerPixel;
            const size_t targetIndex =
                (static_cast<size_t>(y) * width + x) * 3;
            loaded.pixels[targetIndex] = bytes[sourceIndex + 2];
            loaded.pixels[targetIndex + 1] = bytes[sourceIndex + 1];
            loaded.pixels[targetIndex + 2] = bytes[sourceIndex];
        }
    }

    image = move(loaded);
    error.clear();
    return true;
}

bool saveBmp(const string& path, const Image& image, string& error) {
    const uint64_t pixelCount =
        static_cast<uint64_t>(image.width) * image.height;
    if (image.width == 0 || image.height == 0 ||
        image.width > static_cast<uint32_t>(numeric_limits<int32_t>::max()) ||
        image.height > static_cast<uint32_t>(numeric_limits<int32_t>::max()) ||
        pixelCount > kMaximumPixels || image.pixels.size() != pixelCount * 3) {
        error = "The image data is invalid and cannot be saved.";
        return false;
    }

    const uint64_t rowSize =
        ((static_cast<uint64_t>(image.width) * 24 + 31) / 32) * 4;
    const uint64_t pixelBytes = rowSize * image.height;
    const uint64_t fileSize = 54 + pixelBytes;
    if (fileSize > numeric_limits<uint32_t>::max()) {
        error = "The image is too large to save as a BMP file.";
        return false;
    }

    ofstream output(path, ios::binary);
    if (!output) {
        error = "Could not create the BMP image file.";
        return false;
    }

    output.put('B');
    output.put('M');
    writeU32(output, static_cast<uint32_t>(fileSize));
    writeU32(output, 0);
    writeU32(output, 54);
    writeU32(output, 40);
    writeU32(output, image.width);
    writeU32(output, image.height);
    writeU16(output, 1);
    writeU16(output, 24);
    writeU32(output, 0);
    writeU32(output, static_cast<uint32_t>(pixelBytes));
    writeU32(output, 2835);
    writeU32(output, 2835);
    writeU32(output, 0);
    writeU32(output, 0);

    const size_t paddingSize = static_cast<size_t>(rowSize - image.width * 3);
    const char padding[3] = {0, 0, 0};
    for (uint32_t y = 0; y < image.height; ++y) {
        const uint32_t sourceY = image.height - 1 - y;
        const size_t rowStart =
            static_cast<size_t>(sourceY) * image.width * 3;
        for (uint32_t x = 0; x < image.width; ++x) {
            const size_t index = rowStart + static_cast<size_t>(x) * 3;
            output.put(static_cast<char>(image.pixels[index + 2]));
            output.put(static_cast<char>(image.pixels[index + 1]));
            output.put(static_cast<char>(image.pixels[index]));
        }
        output.write(padding, static_cast<streamsize>(paddingSize));
    }

    if (!output) {
        error = "An error occurred while writing the BMP image.";
        return false;
    }

    error.clear();
    return true;
}

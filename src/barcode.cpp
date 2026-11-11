#include "barcode.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <vector>

using namespace std;

namespace {

struct Code39Entry {
    char character;
    const char* pattern;
};

constexpr Code39Entry kCode39Patterns[] = {
    {'0', "nnnwwnwnn"},
    {'1', "wnnwnnnnw"},
    {'2', "nnwwnnnnw"},
    {'3', "wnwwnnnnn"},
    {'4', "nnnwwnnnw"},
    {'5', "wnnwwnnnn"},
    {'6', "nnwwwnnnn"},
    {'7', "nnnwnnwnw"},
    {'8', "wnnwnnwnn"},
    {'9', "nnwwnnwnn"},
    {'*', "nwnnwnwnn"}
};

constexpr uint32_t kModulePixels = 3;
constexpr uint32_t kQuietZoneModules = 10;
constexpr uint32_t kBarHeight = 120;
constexpr uint32_t kVerticalMargin = 10;

const char* findPattern(char character) {
    for (size_t i = 0; i < sizeof(kCode39Patterns) / sizeof(kCode39Patterns[0]); i++) {
        const Code39Entry& entry = kCode39Patterns[i];
        if (entry.character == character) {
            return entry.pattern;
        }
    }
    return nullptr;
}

vector<bool> createModules(const string& digits) {
    vector<bool> modules(kQuietZoneModules, false);
    const string encoded = "*" + digits + "*";

    for (size_t characterIndex = 0; characterIndex < encoded.size(); ++characterIndex) {
        const char* pattern = findPattern(encoded[characterIndex]);
        if (pattern == nullptr) {
            return {};
        }

        for (size_t element = 0; element < 9; ++element) {
            const size_t elementWidth = pattern[element] == 'w' ? 3 : 1;
            const bool isBar = element % 2 == 0;
            modules.insert(modules.end(), elementWidth, isBar);
        }
        if (characterIndex + 1 < encoded.size()) {
            modules.push_back(false);
        }
    }

    modules.insert(modules.end(), kQuietZoneModules, false);
    return modules;
}

bool decodeRow(const Image& image, uint32_t y, string& digits) {
    uint8_t minimum = 255;
    uint8_t maximum = 0;
    const size_t rowStart = static_cast<size_t>(y) * image.width * 3;
    for (uint32_t x = 0; x < image.width; ++x) {
        const size_t index = rowStart + static_cast<size_t>(x) * 3;
        const auto gray = static_cast<uint8_t>(
            (static_cast<unsigned int>(image.pixels[index]) +
             image.pixels[index + 1] + image.pixels[index + 2]) / 3);
        minimum = min(minimum, gray);
        maximum = max(maximum, gray);
    }
    if (maximum - minimum < 50) {
        return false;
    }

    const uint8_t threshold =
        static_cast<uint8_t>((static_cast<unsigned int>(minimum) + maximum) / 2);
    struct Run {
        bool isBar;
        uint32_t width;
    };

    vector<Run> runs;
    bool currentIsBar = false;
    uint32_t currentWidth = 0;
    for (uint32_t x = 0; x < image.width; ++x) {
        const size_t index = rowStart + static_cast<size_t>(x) * 3;
        const auto gray = static_cast<uint8_t>(
            (static_cast<unsigned int>(image.pixels[index]) +
             image.pixels[index + 1] + image.pixels[index + 2]) / 3);
        const bool isBar = gray <= threshold;
        if (currentWidth == 0) {
            currentIsBar = isBar;
            currentWidth = 1;
        } else if (isBar == currentIsBar) {
            ++currentWidth;
        } else {
            runs.push_back({currentIsBar, currentWidth});
            currentIsBar = isBar;
            currentWidth = 1;
        }
    }
    if (currentWidth != 0) {
        runs.push_back({currentIsBar, currentWidth});
    }

    while (!runs.empty() && !runs.front().isBar) {
        runs.erase(runs.begin());
    }
    while (!runs.empty() && !runs.back().isBar) {
        runs.pop_back();
    }

    constexpr size_t kCharacterCount = 8;
    constexpr size_t kRunsPerCharacter = 9;
    constexpr size_t kSeparatorCount = kCharacterCount - 1;
    if (runs.size() != kCharacterCount * kRunsPerCharacter + kSeparatorCount ||
        runs.front().isBar == false || runs.back().isBar == false) {
        return false;
    }

    vector<uint32_t> sortedWidths;
    sortedWidths.reserve(runs.size());
    for (size_t i = 0; i < runs.size(); i++) {
        sortedWidths.push_back(runs[i].width);
    }
    sort(sortedWidths.begin(), sortedWidths.end());

    size_t split = 0;
    double largestRatio = 1.8;
    for (size_t i = 0; i + 1 < sortedWidths.size(); ++i) {
        const double ratio = static_cast<double>(sortedWidths[i + 1]) / sortedWidths[i];
        if (ratio > largestRatio) {
            largestRatio = ratio;
            split = i + 1;
        }
    }
    if (split == 0) {
        return false;
    }

    const double narrowWidth = accumulate(
        sortedWidths.begin(), sortedWidths.begin() + split, 0.0) / split;
    if (sortedWidths[split] / narrowWidth < 2.0 ||
        sortedWidths.back() / narrowWidth > 4.5) {
        return false;
    }

    string decoded;
    for (size_t characterIndex = 0; characterIndex < kCharacterCount; ++characterIndex) {
        const size_t runStart = characterIndex * (kRunsPerCharacter + 1);
        array<char, kRunsPerCharacter> pattern{};
        for (size_t element = 0; element < kRunsPerCharacter; ++element) {
            const Run& run = runs[runStart + element];
            if (run.isBar != (element % 2 == 0)) {
                return false;
            }
            pattern[element] = run.width / narrowWidth > 2.0 ? 'w' : 'n';
        }

        char character = '\0';
        for (size_t i = 0; i < sizeof(kCode39Patterns) / sizeof(kCode39Patterns[0]); i++) {
            const Code39Entry& entry = kCode39Patterns[i];
            if (equal(pattern.begin(), pattern.end(), entry.pattern)) {
                character = entry.character;
                break;
            }
        }
        if (character == '\0') {
            return false;
        }
        decoded.push_back(character);

        if (characterIndex + 1 < kCharacterCount) {
            const Run& separator = runs[runStart + kRunsPerCharacter];
            if (separator.isBar || separator.width / narrowWidth > 2.0) {
                return false;
            }
        }
    }

    if (decoded.front() != '*' || decoded.back() != '*') {
        return false;
    }
    const string result = decoded.substr(1, decoded.size() - 2);
    if (!isValidBarcodeNumber(result)) {
        return false;
    }

    digits = result;
    return true;
}

}  // namespace

bool isValidBarcodeNumber(const string& digits) {
    if (digits.size() != 6 || digits.front() < '1' || digits.front() > '9') {
        return false;
    }
    for (size_t i = 0; i < digits.size(); i++) {
        if (digits[i] < '0' || digits[i] > '9') {
            return false;
        }
    }
    return true;
}

optional<Image> createBarcodeImage(const string& digits) {
    if (!isValidBarcodeNumber(digits)) {
        return nullopt;
    }

    const vector<bool> modules = createModules(digits);
    Image image;
    image.width = static_cast<uint32_t>(modules.size()) * kModulePixels;
    image.height = kBarHeight + 2 * kVerticalMargin;
    image.pixels.assign(static_cast<size_t>(image.width) * image.height * 3, 255);

    for (size_t module = 0; module < modules.size(); ++module) {
        if (!modules[module]) {
            continue;
        }
        for (uint32_t y = kVerticalMargin; y < kVerticalMargin + kBarHeight; ++y) {
            for (uint32_t x = 0; x < kModulePixels; ++x) {
                const size_t pixel =
                    (static_cast<size_t>(y) * image.width +
                     module * kModulePixels + x) * 3;
                image.pixels[pixel] = 0;
                image.pixels[pixel + 1] = 0;
                image.pixels[pixel + 2] = 0;
            }
        }
    }

    return image;
}

string createAsciiPreview(const string& digits) {
    const vector<bool> modules = createModules(digits);
    string row;
    row.reserve(modules.size());
    for (size_t i = 0; i < modules.size(); i++) {
        row.push_back(modules[i] ? '#' : ' ');
    }
    return row;
}

bool decodeBarcodeImage(const Image& image, string& digits) {
    if (image.width == 0 || image.height == 0 ||
        image.pixels.size() != static_cast<size_t>(image.width) * image.height * 3) {
        return false;
    }

    const uint32_t step = max<uint32_t>(1, image.height / 40);
    for (uint32_t y = image.height / 5; y < image.height * 4 / 5; y += step) {
        if (decodeRow(image, y, digits)) {
            return true;
        }
    }
    return false;
}

#include "barcode.h"
#include "image.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>

using namespace std;

namespace {

bool check(bool condition, const string& description) {
    if (!condition) {
        cerr << "FAILED: " << description << '\n';
        return false;
    }
    return true;
}

}  // namespace

int main() {
    bool passed = true;
    passed &= check(isValidBarcodeNumber("123456"), "valid number is accepted");
    passed &= check(!isValidBarcodeNumber("012345"), "leading zero is rejected");
    passed &= check(!isValidBarcodeNumber("12345"), "short number is rejected");
    passed &= check(!isValidBarcodeNumber("12345a"), "non-digit is rejected");
    passed &= check(!createBarcodeImage("012345").has_value(),
                    "invalid number cannot be encoded");

    const auto uniquePart = chrono::steady_clock::now().time_since_epoch().count();
    const string numbers[] = {"109876", "583204"};
    for (size_t i = 0; i < 2; ++i) {
        const auto image = createBarcodeImage(numbers[i]);
        passed &= check(image.has_value(), "valid number creates an image");
        if (!image) {
            continue;
        }

        const filesystem::path path =
            filesystem::temp_directory_path() /
            ("store_barcode_test_" + to_string(uniquePart) + "_" +
             to_string(i) + ".bmp");
        string error;
        passed &= check(saveBmp(path.string(), *image, error), "image is saved as BMP");

        Image loaded;
        passed &= check(loadBmp(path.string(), loaded, error), "saved BMP can be loaded");
        string decoded;
        passed &= check(decodeBarcodeImage(loaded, decoded),
                        "saved image contains a readable barcode");
        passed &= check(decoded == numbers[i], "decoded number matches the original");

        error_code removeError;
        filesystem::remove(path, removeError);
        passed &= check(!removeError, "temporary BMP is removed");
    }
    return passed ? 0 : 1;
}

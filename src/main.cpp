#include "barcode.h"
#include "image.h"

#include <iostream>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

using namespace std;

namespace {

const char* RESET = "\033[0m";
const char* BOLD = "\033[1m";
const char* CYAN = "\033[36m";
const char* BLUE = "\033[34m";
const char* GREEN = "\033[32m";
const char* YELLOW = "\033[33m";
const char* RED = "\033[31m";

void showMenu() {
    cout << "\n";
    cout << BLUE << "  +--------------------------------------+\n";
    cout << "  |" << RESET << BOLD << CYAN
         << "        SMALL STORE BARCODE TOOL      " << RESET << BLUE << "|\n";
    cout << "  +--------------------------------------+\n";
    cout << "  |  " << GREEN << "1." << RESET << " Generate a barcode               "
         << BLUE << "|\n";
    cout << "  |  " << YELLOW << "2." << RESET << " Read a barcode from a BMP image  "
         << BLUE << "|\n";
    cout << "  |  " << RED << "0." << RESET << " Exit                             "
         << BLUE << "|\n";
    cout << "  +--------------------------------------+" << RESET << "\n";
    cout << "  " << BOLD << "Choose an option: " << RESET;
}

void showBarcode(const string& digits) {
    const string barcode = createAsciiPreview(digits);
    int terminalWidth = 80;

#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO consoleInfo;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &consoleInfo)) {
        terminalWidth = consoleInfo.srWindow.Right - consoleInfo.srWindow.Left + 1;
    }
#endif

    const size_t wideWidth = terminalWidth >= 103 ? 2 : 1;
    size_t firstBar = 0;
    size_t lastBar = barcode.size();

    while (firstBar < lastBar && barcode[firstBar] == ' ') {
        firstBar++;
    }
    while (lastBar > firstBar && barcode[lastBar - 1] == ' ') {
        lastBar--;
    }

    cout << "\n  " << CYAN << "Barcode preview:" << RESET << "\n\n";
    cout << BOLD;
    for (size_t i = firstBar; i < lastBar;) {
        size_t runEnd = i + 1;
        while (runEnd < lastBar && barcode[runEnd] == barcode[i]) {
            runEnd++;
        }

        size_t displayWidth = 1;
        if (runEnd - i == 3) {
            displayWidth = wideWidth;
        }

        for (size_t column = 0; column < displayWidth; column++) {
            if (barcode[i] == '#') {
                cout << u8"█";
            } else {
                cout << ' ';
            }
        }
        i = runEnd;
    }
    cout << RESET << "\n  " << digits << '\n';
}

void generateBarcode() {
    string digits;
    cout << "\n  " << YELLOW
         << "Enter a 6-digit number (first digit must be 1-9): " << RESET;
    if (!getline(cin, digits)) {
        return;
    }

    if (!isValidBarcodeNumber(digits)) {
        cout << "  " << RED
             << "Invalid input. Please enter exactly 6 digits, starting with 1-9."
             << RESET << '\n';
        return;
    }

    const string defaultPath = "barcode_" + digits + ".bmp";
    string path;
    cout << "  " << YELLOW << "Save image as [" << defaultPath << "]: " << RESET;
    if (!getline(cin, path)) {
        return;
    }
    if (path.empty()) {
        path = defaultPath;
    }

    const auto image = createBarcodeImage(digits);
    if (!image) {
        cout << "  " << RED << "Could not create the barcode image." << RESET << '\n';
        return;
    }

    string error;
    if (!saveBmp(path, *image, error)) {
        cout << "  " << RED << "Error: " << error << RESET << '\n';
        return;
    }

    showBarcode(digits);
    cout << "\n  " << GREEN << "Image saved successfully: " << path << RESET << '\n';
}

void readBarcode() {
    string path;
    cout << "\n  " << YELLOW << "Enter the path to a BMP image: " << RESET;
    if (!getline(cin, path)) {
        return;
    }
    if (path.empty()) {
        cout << "  " << RED << "The image path cannot be empty." << RESET << '\n';
        return;
    }

    Image image;
    string error;
    if (!loadBmp(path, image, error)) {
        cout << "  " << RED << "Error: " << error << RESET << '\n';
        return;
    }

    string digits;
    if (!decodeBarcodeImage(image, digits)) {
        cout << "  " << RED
             << "No supported 6-digit barcode was found in that image."
             << RESET << '\n';
        return;
    }

    cout << "\n  " << GREEN << BOLD << "Barcode number: " << digits << RESET << '\n';
}

}  // namespace

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    cout << "\n  " << BOLD << CYAN
         << "Welcome to the Small Store Barcode Tool" << RESET << '\n';

    while (true) {
        showMenu();

        string choice;
        if (!getline(cin, choice) || choice == "0") {
            break;
        }

        if (choice == "1") {
            generateBarcode();
        } else if (choice == "2") {
            readBarcode();
        } else {
            cout << "  " << RED << "Please choose 1, 2, or 0." << RESET << '\n';
        }
    }

    cout << "\n  " << CYAN << "Thank you. Goodbye!" << RESET << "\n\n";
    return 0;
}

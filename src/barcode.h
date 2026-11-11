#ifndef STORE_BARCODE_H
#define STORE_BARCODE_H

#include "image.h"

#include <optional>
#include <string>

bool isValidBarcodeNumber(const std::string& digits);
std::optional<Image> createBarcodeImage(const std::string& digits);
std::string createAsciiPreview(const std::string& digits);
bool decodeBarcodeImage(const Image& image, std::string& digits);

#endif

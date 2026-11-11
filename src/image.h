#ifndef STORE_IMAGE_H
#define STORE_IMAGE_H

#include <cstdint>
#include <string>
#include <vector>

struct Image {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> pixels;
};

bool loadBmp(const std::string& path, Image& image, std::string& error);
bool saveBmp(const std::string& path, const Image& image, std::string& error);

#endif

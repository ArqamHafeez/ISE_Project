#include "camera_frame_decoder.h"

#include "barcode.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>

#include <opencv2/imgproc.hpp>

namespace {

bool rotateFrame(const cv::Mat& frame, double degrees, cv::Mat& rotated) {
    const cv::Point2f center(
        static_cast<float>(frame.cols) / 2.0F,
        static_cast<float>(frame.rows) / 2.0F);
    cv::Mat transform = cv::getRotationMatrix2D(center, degrees, 1.0);
    const double cosine = std::abs(transform.at<double>(0, 0));
    const double sine = std::abs(transform.at<double>(0, 1));
    const int width = static_cast<int>(frame.rows * sine + frame.cols * cosine);
    const int height = static_cast<int>(frame.rows * cosine + frame.cols * sine);
    transform.at<double>(0, 2) += width / 2.0 - center.x;
    transform.at<double>(1, 2) += height / 2.0 - center.y;

    cv::warpAffine(frame, rotated, transform, cv::Size(width, height),
                   cv::INTER_LINEAR, cv::BORDER_CONSTANT,
                   cv::Scalar(255, 255, 255));
    return !rotated.empty();
}

bool decodeAtAngle(const cv::Mat& frame, double degrees, std::string& barcode) {
    cv::Mat rotated;
    const cv::Mat* source = &frame;
    if (degrees != 0) {
        if (!rotateFrame(frame, degrees, rotated)) {
            return false;
        }
        source = &rotated;
    }

    cv::Mat rgbFrame;
    cv::cvtColor(*source, rgbFrame, cv::COLOR_GRAY2RGB);

    Image image;
    image.width = static_cast<std::uint32_t>(rgbFrame.cols);
    image.height = static_cast<std::uint32_t>(rgbFrame.rows);
    image.pixels.resize(static_cast<std::size_t>(rgbFrame.rows) *
                        rgbFrame.cols * 3);
    for (int y = 0; y < rgbFrame.rows; ++y) {
        const std::uint8_t* sourceRow = rgbFrame.ptr<std::uint8_t>(y);
        std::uint8_t* destinationRow = image.pixels.data() +
            static_cast<std::size_t>(y) * rgbFrame.cols * 3;
        std::memcpy(destinationRow, sourceRow,
                    static_cast<std::size_t>(rgbFrame.cols) * 3);
    }

    return decodeBarcodeImage(image, barcode);
}

}  // namespace

bool decodeCameraFrame(const cv::Mat& frame, std::string& barcode) {
    if (frame.empty() || frame.type() != CV_8UC3) {
        return false;
    }

    cv::Mat grayFrame;
    cv::cvtColor(frame, grayFrame, cv::COLOR_BGR2GRAY);
    cv::medianBlur(grayFrame, grayFrame, 3);

    constexpr std::array<double, 7> kRotationAngles = {
        0, -5, 5, -10, 10, -15, 15
    };
    for (double angle : kRotationAngles) {
        if (decodeAtAngle(grayFrame, angle, barcode)) {
            return true;
        }
    }
    return false;
}

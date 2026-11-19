#include "barcode.h"
#include "camera_frame_decoder.h"

#include <cassert>
#include <string>

#include <opencv2/imgproc.hpp>

namespace {

cv::Mat makeCameraFrame() {
    const auto barcode = createBarcodeImage("123456");
    assert(barcode.has_value());

    cv::Mat frame(720, 1280, CV_8UC3, cv::Scalar(255, 255, 255));
    cv::Mat barcodeFrame(
        static_cast<int>(barcode->height),
        static_cast<int>(barcode->width),
        CV_8UC3);
    for (int y = 0; y < barcodeFrame.rows; ++y) {
        cv::Vec3b* row = barcodeFrame.ptr<cv::Vec3b>(y);
        for (int x = 0; x < barcodeFrame.cols; ++x) {
            const std::size_t pixel =
                (static_cast<std::size_t>(y) * barcode->width + x) * 3;
            row[x] = cv::Vec3b(
                barcode->pixels[pixel + 2],
                barcode->pixels[pixel + 1],
                barcode->pixels[pixel]);
        }
    }

    const int left = (frame.cols - barcodeFrame.cols) / 2;
    const int top = (frame.rows - barcodeFrame.rows) / 2;
    barcodeFrame.copyTo(
        frame(cv::Rect(left, top, barcodeFrame.cols, barcodeFrame.rows)));
    return frame;
}

cv::Mat rotateForTest(const cv::Mat& frame, double degrees) {
    const cv::Point2f center(
        static_cast<float>(frame.cols) / 2.0F,
        static_cast<float>(frame.rows) / 2.0F);
    const cv::Mat transform = cv::getRotationMatrix2D(center, degrees, 1.0);
    cv::Mat rotated;
    cv::warpAffine(frame, rotated, transform, frame.size(),
                   cv::INTER_LINEAR, cv::BORDER_CONSTANT,
                   cv::Scalar(255, 255, 255));
    return rotated;
}

void checkBarcode(const cv::Mat& frame) {
    std::string decoded;
    assert(decodeCameraFrame(frame, decoded));
    assert(decoded == "123456");
}

}  // namespace

int main() {
    const cv::Mat frame = makeCameraFrame();
    checkBarcode(frame);
    checkBarcode(rotateForTest(frame, 12));
    checkBarcode(rotateForTest(frame, -12));

    cv::Mat blurred;
    cv::GaussianBlur(frame, blurred, cv::Size(3, 3), 0.8);
    checkBarcode(blurred);

    cv::Mat smaller;
    cv::resize(frame, smaller, cv::Size(640, 360), 0, 0, cv::INTER_AREA);
    cv::Mat resized;
    cv::resize(smaller, resized, frame.size(), 0, 0, cv::INTER_LINEAR);
    checkBarcode(resized);
    return 0;
}

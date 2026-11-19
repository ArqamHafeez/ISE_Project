#ifndef CAMERA_FRAME_DECODER_H
#define CAMERA_FRAME_DECODER_H

#include <opencv2/core/mat.hpp>

#include <string>

bool decodeCameraFrame(const cv::Mat& frame, std::string& barcode);

#endif

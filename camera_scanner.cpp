#include "camera_scanner.h"

#include "camera_frame_decoder.h"

#include <opencv2/imgproc.hpp>

CameraScanner::CameraScanner(QObject* parent) : QObject(parent) {
    connect(&frameTimer, &QTimer::timeout, this, &CameraScanner::readFrame);
    barcodeClearTimer.setSingleShot(true);
    connect(&barcodeClearTimer, &QTimer::timeout,
            this, &CameraScanner::clearLastBarcode);
    scanTimeoutTimer.setSingleShot(true);
    connect(&scanTimeoutTimer, &QTimer::timeout,
            this, &CameraScanner::scanTimedOut);
}

CameraScanner::~CameraScanner() {
    stopCamera();
}

bool CameraScanner::startCamera(QString& error) {
    if (camera.isOpened()) {
        return true;
    }

    try {
        if (!camera.open(0)) {
            error = "Could not open the laptop camera.";
            return false;
        }

        camera.set(cv::CAP_PROP_FRAME_WIDTH, 1280);
        camera.set(cv::CAP_PROP_FRAME_HEIGHT, 720);
    } catch (const cv::Exception& exception) {
        error = QString::fromStdString(exception.what());
        return false;
    }

    scanClock.start();
    frameTimer.start(33);
    scanTimeoutTimer.start(8000);
    error.clear();
    return true;
}

void CameraScanner::stopCamera() {
    frameTimer.stop();
    barcodeClearTimer.stop();
    scanTimeoutTimer.stop();
    lastBarcode.clear();
    if (camera.isOpened()) {
        camera.release();
    }
}

void CameraScanner::clearLastBarcode() {
    lastBarcode.clear();
}

void CameraScanner::scanTimedOut() {
    emit scanFailed(
        "No barcode detected yet. Keep it centered, sharp, and well lit; "
        "automatic scanning will continue.");
}

void CameraScanner::readFrame() {
    cv::Mat frame;
    try {
        if (!camera.read(frame) || frame.empty()) {
            emit scannerError("Could not read a frame from the camera.");
            stopCamera();
            return;
        }

        cv::Mat rgbFrame;
        cv::cvtColor(frame, rgbFrame, cv::COLOR_BGR2RGB);
        const QImage preview(rgbFrame.data, rgbFrame.cols, rgbFrame.rows,
                             static_cast<int>(rgbFrame.step), QImage::Format_RGB888);
        emit frameReady(preview.copy());

        if (scanClock.elapsed() < 200) {
            return;
        }
        scanClock.restart();

        std::string barcode;
        if (decodeCameraFrame(frame, barcode)) {
            const QString decodedBarcode = QString::fromStdString(barcode);
            barcodeClearTimer.start(700);
            scanTimeoutTimer.start(8000);
            if (decodedBarcode != lastBarcode) {
                lastBarcode = decodedBarcode;
                emit barcodeFound(decodedBarcode);
            }
        }
    } catch (const cv::Exception& exception) {
        stopCamera();
        emit scannerError(QString::fromStdString(exception.what()));
    }
}

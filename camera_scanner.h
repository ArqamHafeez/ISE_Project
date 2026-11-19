#ifndef CAMERA_SCANNER_H
#define CAMERA_SCANNER_H

#include <QObject>
#include <QImage>
#include <QElapsedTimer>
#include <QString>
#include <QTimer>

#include <opencv2/videoio.hpp>

class CameraScanner : public QObject {
    Q_OBJECT

public:
    explicit CameraScanner(QObject* parent = nullptr);
    ~CameraScanner();

    bool startCamera(QString& error);
    void stopCamera();

signals:
    void frameReady(const QImage& frame);
    void barcodeFound(const QString& barcode);
    void scanFailed(const QString& message);
    void scannerError(const QString& error);

private slots:
    void readFrame();
    void clearLastBarcode();
    void scanTimedOut();

private:
    cv::VideoCapture camera;
    QTimer frameTimer;
    QTimer barcodeClearTimer;
    QTimer scanTimeoutTimer;
    QElapsedTimer scanClock;
    QString lastBarcode;
};

#endif

#ifndef MAIN_WINDOW_H
#define MAIN_WINDOW_H

#include "pos_service.h"
#include "store_repository.h"

#include <QMainWindow>
#include <QString>

class CameraScanner;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTabWidget;
class QTableWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    void startCamera();
    void stopCamera();
    void handleBarcode(const QString& barcode);
    void showDescription();
    void addTypedBarcode();
    void removeSelectedItem();
    void completeOrder();
    void updateRole();
    void updateCart();
    void updatePaymentFields();
    void updateOrderStatus();
    void setStatus(const QString& text, const QString& state);

private:
    void buildInterface();
    void showProduct(const Product& product);
    void showProductByBarcode(const QString& barcode);
    void showBill(const Order& order, const BillSummary& bill);

    StoreRepository repository;
    PosService service;
    CameraScanner* cameraScanner = nullptr;

    QComboBox* roleCombo = nullptr;
    QTabWidget* tabs = nullptr;
    QWidget* descriptionPage = nullptr;
    QWidget* orderPage = nullptr;
    QLabel* cameraPreview = nullptr;
    QLabel* descriptionText = nullptr;
    QLabel* orderDescriptionText = nullptr;
    QLabel* cartSummaryLabel = nullptr;
    QLabel* statusText = nullptr;
    QPushButton* startCameraButton = nullptr;
    QPushButton* stopCameraButton = nullptr;
    QLineEdit* barcodeInput = nullptr;
    QDoubleSpinBox* quantityInput = nullptr;
    QTableWidget* cartTable = nullptr;
    QLineEdit* customerNameInput = nullptr;
    QLineEdit* customerContactInput = nullptr;
    QDoubleSpinBox* paymentAmountInput = nullptr;
    QComboBox* paymentModeInput = nullptr;
    QLineEdit* merchantInput = nullptr;
    QComboBox* orderStatusInput = nullptr;
    QString lastScannedBarcode;
    QString lastAddedBarcode;
    bool productsLoaded = false;
};

#endif

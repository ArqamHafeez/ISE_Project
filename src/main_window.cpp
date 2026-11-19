#include "main_window.h"

#include "camera_scanner.h"

#include <QAbstractItemView>
#include <QColor>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPaintEvent>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QPolygon>
#include <QScrollArea>
#include <QSizePolicy>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QVBoxLayout>
#include <QStyle>
#include <QStyleOptionSpinBox>
#include <QWheelEvent>

#include <filesystem>
#include <iomanip>
#include <sstream>
#include <vector>

using namespace std;

namespace {

class NoWheelDoubleSpinBox : public QDoubleSpinBox {
public:
    using QDoubleSpinBox::QDoubleSpinBox;

protected:
    void wheelEvent(QWheelEvent* event) override {
        event->ignore();
    }

    void paintEvent(QPaintEvent* event) override {
        QDoubleSpinBox::paintEvent(event);

        QStyleOptionSpinBox option;
        initStyleOption(&option);
        const QRect upRect = style()->subControlRect(
            QStyle::CC_SpinBox, &option, QStyle::SC_SpinBoxUp, this);
        const QRect downRect = style()->subControlRect(
            QStyle::CC_SpinBox, &option, QStyle::SC_SpinBoxDown, this);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor("#193b56"));

        const int halfWidth = 5;
        const int halfHeight = 3;
        const QPoint upCenter = upRect.center();
        QPolygon upArrow;
        upArrow << QPoint(upCenter.x(), upCenter.y() - halfHeight)
                << QPoint(upCenter.x() - halfWidth, upCenter.y() + halfHeight)
                << QPoint(upCenter.x() + halfWidth, upCenter.y() + halfHeight);
        painter.drawPolygon(upArrow);

        const QPoint downCenter = downRect.center();
        QPolygon downArrow;
        downArrow << QPoint(downCenter.x() - halfWidth, downCenter.y() - halfHeight)
                  << QPoint(downCenter.x() + halfWidth, downCenter.y() - halfHeight)
                  << QPoint(downCenter.x(), downCenter.y() + halfHeight);
        painter.drawPolygon(downArrow);
    }
};

class NoWheelComboBox : public QComboBox {
public:
    using QComboBox::QComboBox;

protected:
    void wheelEvent(QWheelEvent* event) override {
        event->ignore();
    }
};

QDoubleSpinBox* makeMoneyInput(QWidget* parent) {
    QDoubleSpinBox* input = new NoWheelDoubleSpinBox(parent);
    input->setDecimals(2);
    input->setRange(0, 1000000000);
    input->setSingleStep(1);
    return input;
}

string moneyText(double amount) {
    ostringstream output;
    output << fixed << setprecision(2) << amount;
    return output.str();
}

}  // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      repository(std::filesystem::current_path()),
      service(repository) {
    setWindowTitle("Small Store POS");
      resize(1100, 860);
      setMinimumSize(600, 560);
    buildInterface();

    string error;
    productsLoaded = service.loadProducts(error);
    if (!productsLoaded) {
        setStatus("Product file unavailable", "error");
        QMessageBox::critical(
            this, "Product file error",
            QString::fromStdString(error) +
            "\n\nPlace Product.txt in the application's working directory "
            "and restart the program.");
    } else if (service.products().empty()) {
        setStatus("Product.txt is empty", "warning");
        QMessageBox::warning(
            this, "No products",
            "Product.txt is empty. Add product rows before scanning or recording sales.");
    } else {
        setStatus("Products loaded. Start the camera when ready.", "info");
    }

    updateRole();
    updateCart();
    if (productsLoaded && !service.products().empty()) {
        QTimer::singleShot(0, this, [this]() { startCamera(); });
    }
}

MainWindow::~MainWindow() {
    if (cameraScanner != nullptr) {
        cameraScanner->stopCamera();
    }
}

void MainWindow::buildInterface() {
    setStyleSheet(R"(
        QMainWindow, QWidget {
            color: #172b4d;
            font-family: "Segoe UI";
            font-size: 10pt;
        }
        QWidget#centralPanel {
            background: #f3f6fb;
        }
        QFrame#headerPanel {
            background: #102a43;
            border: none;
            border-radius: 12px;
        }
        QLabel#appTitle {
            color: #ffffff;
            font-size: 19pt;
            font-weight: 700;
        }
        QLabel#appSubtitle, QLabel#roleLabel {
            color: #c9d8e8;
        }
        QComboBox, QLineEdit, QDoubleSpinBox {
            background: #ffffff;
            border: 1px solid #cbd5e1;
            border-radius: 6px;
            padding: 7px 9px;
            min-height: 20px;
        }
        QComboBox:focus, QLineEdit:focus, QDoubleSpinBox:focus {
            border: 1px solid #168aad;
        }
        QDoubleSpinBox {
            padding-right: 30px;
        }
        QDoubleSpinBox::up-button, QDoubleSpinBox::down-button {
            subcontrol-origin: border;
            width: 25px;
            background: #d7e6f3;
            border-left: 1px solid #aebfd1;
        }
        QDoubleSpinBox::up-button {
            subcontrol-position: top right;
            border-top-right-radius: 5px;
        }
        QDoubleSpinBox::down-button {
            subcontrol-position: bottom right;
            border-bottom-right-radius: 5px;
        }
        QDoubleSpinBox::up-button:hover, QDoubleSpinBox::down-button:hover {
            background: #9dc9d4;
        }
        QDoubleSpinBox::up-button:pressed, QDoubleSpinBox::down-button:pressed {
            background: #087e8b;
        }
        QPushButton {
            background: #e6edf5;
            border: 1px solid #d5deea;
            border-radius: 7px;
            padding: 8px 14px;
            min-height: 20px;
            font-weight: 600;
        }
        QPushButton:hover {
            background: #d7e5f1;
        }
        QPushButton:disabled {
            color: #8291a3;
            background: #e9edf2;
        }
        QPushButton#primaryButton {
            color: #ffffff;
            background: #087e8b;
            border: none;
        }
        QPushButton#primaryButton:hover {
            background: #066874;
        }
        QTabWidget::pane {
            background: #ffffff;
            border: 1px solid #dce4ee;
            border-radius: 8px;
            top: -1px;
        }
        QScrollArea {
            background: #f3f6fb;
            border: none;
        }
        QScrollBar:vertical {
            width: 13px;
            background: #e8eef5;
            margin: 3px 2px;
            border-radius: 6px;
        }
        QScrollBar::handle:vertical {
            min-height: 28px;
            background: #9aafc4;
            border-radius: 6px;
        }
        QScrollBar::handle:vertical:hover {
            background: #64829e;
        }
        QTabBar::tab {
            color: #53657a;
            background: #e7edf4;
            border-top-left-radius: 6px;
            border-top-right-radius: 6px;
            padding: 9px 18px;
            margin-right: 4px;
        }
        QTabBar::tab:selected {
            color: #102a43;
            background: #ffffff;
            font-weight: 700;
        }
        QLabel#cameraPreview {
            color: #d9e2ec;
            background: #172b3d;
            border: 1px solid #40566d;
            border-radius: 10px;
        }
        QLabel#statusLabel[state="success"] {
            color: #12613a;
            background: #e1f4e8;
            border: 1px solid #b7e3c5;
        }
        QLabel#statusLabel[state="warning"] {
            color: #815500;
            background: #fff3cd;
            border: 1px solid #f4d889;
        }
        QLabel#statusLabel[state="error"] {
            color: #9c2f2f;
            background: #fde8e7;
            border: 1px solid #f2c1be;
        }
        QLabel#statusLabel[state="info"] {
            color: #22527a;
            background: #e5f1fb;
            border: 1px solid #c7dff2;
        }
        QLabel#statusLabel {
            border-radius: 7px;
            padding: 8px 11px;
        }
        QLabel#productInfoText {
            color: #334e68;
            background: #f5f8fc;
            border: 1px solid #e0e8f0;
            border-radius: 7px;
            padding: 10px;
        }
        QLabel#cartTitle {
            color: #102a43;
            font-size: 13pt;
            font-weight: 700;
        }
        QLabel#cartSummaryLabel {
            color: #087e8b;
            font-size: 11pt;
            font-weight: 700;
        }
        QTableWidget {
            background: #ffffff;
            alternate-background-color: #f5f8fc;
            border: 1px solid #dce4ee;
            border-radius: 7px;
            gridline-color: #edf1f5;
            selection-background-color: #d6edf0;
            selection-color: #102a43;
        }
        QHeaderView::section {
            color: #42566d;
            background: #edf2f7;
            border: none;
            border-bottom: 1px solid #d5deea;
            padding: 8px;
            font-weight: 700;
        }
    )");

    QScrollArea* screenScroll = new QScrollArea(this);
    screenScroll->setWidgetResizable(true);
    QWidget* central = new QWidget(screenScroll);
    central->setObjectName("centralPanel");
    QVBoxLayout* mainLayout = new QVBoxLayout(central);
    mainLayout->setContentsMargins(18, 14, 18, 16);
    mainLayout->setSpacing(10);

    QFrame* header = new QFrame(central);
    header->setObjectName("headerPanel");
    QHBoxLayout* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(18, 12, 18, 12);
    QVBoxLayout* titleLayout = new QVBoxLayout();
    QLabel* title = new QLabel("Corner Store POS", header);
    title->setObjectName("appTitle");
    QLabel* subtitle = new QLabel("Product lookup  |  Orders  |  Billing", header);
    subtitle->setObjectName("appSubtitle");
    titleLayout->addWidget(title);
    titleLayout->addWidget(subtitle);
    headerLayout->addLayout(titleLayout);
    headerLayout->addStretch();
    QLabel* roleLabel = new QLabel("ROLE", header);
    roleLabel->setObjectName("roleLabel");
    headerLayout->addWidget(roleLabel);
    roleCombo = new NoWheelComboBox(central);
    roleCombo->addItem("Cashier");
    roleCombo->addItem("Manager");
    roleCombo->setMinimumWidth(140);
    headerLayout->addWidget(roleCombo);
    mainLayout->addWidget(header);

    cameraPreview = new QLabel("Camera is stopped", central);
    cameraPreview->setObjectName("cameraPreview");
    cameraPreview->setAlignment(Qt::AlignCenter);
    cameraPreview->setMinimumSize(400, 300);
    cameraPreview->setMaximumSize(640, 480);
    cameraPreview->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    QHBoxLayout* previewLayout = new QHBoxLayout();
    previewLayout->addStretch();
    previewLayout->addWidget(cameraPreview);
    previewLayout->addStretch();
    mainLayout->addLayout(previewLayout);

    QHBoxLayout* cameraButtons = new QHBoxLayout();
    startCameraButton = new QPushButton("Start camera", central);
    startCameraButton->setObjectName("primaryButton");
    stopCameraButton = new QPushButton("Stop camera", central);
    stopCameraButton->setEnabled(false);
    cameraButtons->addWidget(startCameraButton);
    cameraButtons->addWidget(stopCameraButton);
    cameraButtons->addStretch();
    mainLayout->addLayout(cameraButtons);

    statusText = new QLabel("The camera will scan automatically.", central);
    statusText->setObjectName("statusLabel");
    statusText->setProperty("state", "info");
    mainLayout->addWidget(statusText);

    tabs = new QTabWidget(central);
    descriptionPage = new QWidget(tabs);
    orderPage = new QWidget(tabs);
    tabs->addTab(descriptionPage, "Product description");
    tabs->addTab(orderPage, "Record order");
    mainLayout->addWidget(tabs, 1);
    screenScroll->setWidget(central);
    setCentralWidget(screenScroll);

    QVBoxLayout* descriptionLayout = new QVBoxLayout(descriptionPage);
    descriptionText = new QLabel(
        "Scan a product barcode to display its description.", descriptionPage);
    descriptionText->setObjectName("productInfoText");
    descriptionText->setWordWrap(true);
    descriptionText->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    descriptionLayout->addWidget(descriptionText);
    descriptionLayout->addStretch();

    QVBoxLayout* orderLayout = new QVBoxLayout(orderPage);

    QLabel* orderTitle = new QLabel("Current order", orderPage);
    orderTitle->setObjectName("cartTitle");
    orderLayout->addWidget(orderTitle);

    QHBoxLayout* barcodeLayout = new QHBoxLayout();
    barcodeInput = new QLineEdit(orderPage);
    barcodeInput->setPlaceholderText("Barcode from the last camera scan");
    QPushButton* descriptionButton = new QPushButton("Description", orderPage);
    barcodeLayout->addWidget(new QLabel("Scanned barcode:", orderPage));
    barcodeLayout->addWidget(barcodeInput, 1);
    barcodeLayout->addWidget(descriptionButton);
    orderLayout->addLayout(barcodeLayout);

    orderDescriptionText = new QLabel(
        "Product specifications will appear here after scanning.", orderPage);
    orderDescriptionText->setObjectName("productInfoText");
    orderDescriptionText->setWordWrap(true);
    orderLayout->addWidget(orderDescriptionText);

    QHBoxLayout* quantityLayout = new QHBoxLayout();
    quantityInput = new NoWheelDoubleSpinBox(orderPage);
    quantityInput->setDecimals(3);
    quantityInput->setRange(0.001, 1000000);
    quantityInput->setValue(1);
    QPushButton* addButton = new QPushButton("Add to order", orderPage);
    addButton->setObjectName("primaryButton");
    QPushButton* removeButton = new QPushButton("Remove selected item", orderPage);
    quantityLayout->addWidget(new QLabel("Quantity:", orderPage));
    quantityLayout->addWidget(quantityInput);
    quantityLayout->addWidget(addButton);
    quantityLayout->addWidget(removeButton);
    quantityLayout->addStretch();
    orderLayout->addLayout(quantityLayout);

    cartSummaryLabel = new QLabel("No items in the order", orderPage);
    cartSummaryLabel->setObjectName("cartSummaryLabel");
    orderLayout->addWidget(cartSummaryLabel);

    cartTable = new QTableWidget(0, 5, orderPage);
    cartTable->setHorizontalHeaderLabels(
        {"Product", "Barcode", "Quantity", "Unit price (PKR)", "Line total (PKR)"});
    cartTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    cartTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    cartTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    cartTable->setAlternatingRowColors(true);
    cartTable->setShowGrid(false);
    cartTable->verticalHeader()->setVisible(false);
    cartTable->verticalHeader()->setDefaultSectionSize(34);
    cartTable->setMinimumHeight(230);
    orderLayout->addWidget(cartTable, 1);

    QFormLayout* orderForm = new QFormLayout();
    customerNameInput = new QLineEdit(orderPage);
    customerContactInput = new QLineEdit(orderPage);
    paymentAmountInput = makeMoneyInput(orderPage);
    paymentModeInput = new NoWheelComboBox(orderPage);
    paymentModeInput->addItem("Cash", static_cast<int>(PaymentMode::Cash));
    paymentModeInput->addItem("Card", static_cast<int>(PaymentMode::Card));
    paymentModeInput->addItem("Wallet", static_cast<int>(PaymentMode::Wallet));
    merchantInput = new QLineEdit(orderPage);
    merchantInput->setPlaceholderText("e.g. ABL or JazzCash; not needed for cash");
    orderStatusInput = new NoWheelComboBox(orderPage);
    orderStatusInput->addItem("In progress", static_cast<int>(OrderStatus::InProgress));
    orderStatusInput->addItem("On hold", static_cast<int>(OrderStatus::OnHold));
    orderStatusInput->addItem("Paid", static_cast<int>(OrderStatus::Paid));
    orderStatusInput->addItem("Archived", static_cast<int>(OrderStatus::Archived));
    orderStatusInput->setCurrentIndex(2);
    orderForm->addRow("Customer name:", customerNameInput);
    orderForm->addRow("Customer contact (optional):", customerContactInput);
    orderForm->addRow("Payment amount (PKR):", paymentAmountInput);
    orderForm->addRow("Payment mode:", paymentModeInput);
    orderForm->addRow("Payment merchant:", merchantInput);
    orderForm->addRow("Order status:", orderStatusInput);
    orderLayout->addLayout(orderForm);

    QPushButton* billButton = new QPushButton("Generate bill and save order", orderPage);
    orderLayout->addWidget(billButton);

    cameraScanner = new CameraScanner(this);
    connect(startCameraButton, &QPushButton::clicked,
            this, &MainWindow::startCamera);
    connect(stopCameraButton, &QPushButton::clicked,
            this, &MainWindow::stopCamera);
    connect(cameraScanner, &CameraScanner::frameReady, this,
            [this](const QImage& frame) {
                cameraPreview->setPixmap(
                    QPixmap::fromImage(frame).scaled(
                        cameraPreview->size(), Qt::KeepAspectRatio,
                        Qt::SmoothTransformation));
            });
    connect(cameraScanner, &CameraScanner::barcodeFound,
            this, &MainWindow::handleBarcode);
    connect(cameraScanner, &CameraScanner::scanFailed, this,
            [this](const QString& message) {
                setStatus(message, "warning");
            });
    connect(cameraScanner, &CameraScanner::scannerError, this,
            [this](const QString& error) {
                setStatus(error, "error");
                startCameraButton->setEnabled(true);
                stopCameraButton->setEnabled(false);
                cameraPreview->setText("Camera is stopped");
                cameraPreview->setPixmap(QPixmap());
                QMessageBox::warning(this, "Camera error", error);
            });
    connect(roleCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::updateRole);
    connect(descriptionButton, &QPushButton::clicked,
            this, &MainWindow::showDescription);
    connect(addButton, &QPushButton::clicked,
            this, &MainWindow::addTypedBarcode);
    connect(removeButton, &QPushButton::clicked,
            this, &MainWindow::removeSelectedItem);
    connect(billButton, &QPushButton::clicked,
            this, &MainWindow::completeOrder);
    connect(paymentModeInput, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::updatePaymentFields);
    connect(orderStatusInput, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::updateOrderStatus);
    connect(customerNameInput, &QLineEdit::textChanged,
            this, &MainWindow::updateCart);
}

void MainWindow::startCamera() {
    QString error;
    if (!cameraScanner->startCamera(error)) {
        setStatus(error, "error");
        QMessageBox::warning(this, "Camera unavailable", error);
        return;
    }
    startCameraButton->setEnabled(false);
    stopCameraButton->setEnabled(true);
    setStatus("Camera is scanning automatically. Show a barcode in the preview.",
              "success");
}

void MainWindow::stopCamera() {
    cameraScanner->stopCamera();
    cameraPreview->setText("Camera is stopped");
    cameraPreview->setPixmap(QPixmap());
    startCameraButton->setEnabled(true);
    stopCameraButton->setEnabled(false);
    setStatus("Camera stopped.", "info");
}

void MainWindow::handleBarcode(const QString& barcode) {
    lastScannedBarcode = barcode;
    barcodeInput->setText(barcode);

    if (tabs->currentWidget() != orderPage) {
        showProductByBarcode(barcode);
        return;
    }

    string error;
    if (!service.addToCart(barcode.toStdString(), quantityInput->value(), error)) {
        setStatus(QString::fromStdString(error), "error");
        QMessageBox::warning(this, "Could not add product",
                             QString::fromStdString(error));
        return;
    }
    lastAddedBarcode = barcode;
    updateCart();
    const Product* product = service.findProduct(barcode.toStdString());
    if (product == nullptr) {
        setStatus("Product added to the order.", "success");
    } else {
        showProduct(*product);
        setStatus("Added " + QString::fromStdString(product->name) +
                  " to the order.", "success");
    }
}

void MainWindow::showDescription() {
    QString barcode = barcodeInput->text().trimmed();
    if (barcode.isEmpty()) {
        barcode = lastScannedBarcode;
    }
    showProductByBarcode(barcode);
}

void MainWindow::showProductByBarcode(const QString& barcode) {
    if (barcode.isEmpty()) {
        setStatus("Scan or enter a barcode to view product details.", "warning");
        return;
    }
    const Product* product = service.findProduct(barcode.toStdString());
    if (product == nullptr) {
        descriptionText->setText("No product was found for barcode " + barcode + ".");
        orderDescriptionText->setText(
            "No product was found for barcode " + barcode + ".");
        setStatus("Product not found.", "warning");
        return;
    }
    showProduct(*product);
}

void MainWindow::showProduct(const Product& product) {
    QString description;
    description += "Product ID: " + QString::fromStdString(product.id) + "\n";
    description += "Barcode: " + QString::fromStdString(product.barcode) + "\n";
    description += "Acronym: " + QString::fromStdString(product.acronym) + "\n";
    description += "Product name: " + QString::fromStdString(product.name) + "\n";
    description += "Unit: " + QString::fromStdString(unitToString(product.unit)) + "\n";
    description += "Unit price: PKR " +
                   QString::fromStdString(moneyText(product.unitPrice)) + "\n";
    description += "Tax per unit: PKR " +
                   QString::fromStdString(moneyText(product.taxPerUnit)) + "\n";
    description += "Discount per unit: PKR " +
                   QString::fromStdString(moneyText(product.discountPerUnit)) + "\n";
    description += "Available quantity: " +
                   QString::number(product.availableQuantity, 'f', 3) + "\n";
    description += "Barcode image: " +
                   QString::fromStdString(product.barcodePath);
    descriptionText->setText(description);
    orderDescriptionText->setText(description);
    setStatus("Product details loaded.", "success");
}

void MainWindow::addTypedBarcode() {
    string error;
    if (!service.addToCart(barcodeInput->text().trimmed().toStdString(),
                           quantityInput->value(), error)) {
        setStatus(QString::fromStdString(error), "error");
        QMessageBox::warning(this, "Could not add product",
                             QString::fromStdString(error));
        return;
    }
    lastAddedBarcode = barcodeInput->text().trimmed();
    const Product* product =
        service.findProduct(barcodeInput->text().trimmed().toStdString());
    if (product != nullptr) {
        showProduct(*product);
    }
    updateCart();
    setStatus("Product added to the order.", "success");
}

void MainWindow::removeSelectedItem() {
    const int row = cartTable->currentRow();
    if (row < 0) {
        QMessageBox::information(this, "Select a product",
                                 "Select a cart row to remove it.");
        return;
    }
    const QString barcode = cartTable->item(row, 1)->text();
    service.removeFromCart(barcode.toStdString());
    if (lastAddedBarcode == barcode) {
        lastAddedBarcode.clear();
    }
    updateCart();
    setStatus("Product removed from the order.", "info");
}

void MainWindow::updateCart() {
    if (cartTable == nullptr) {
        return;
    }

    const vector<OrderDetail>& details = service.cart();
    cartTable->setRowCount(static_cast<int>(details.size()));
    int highlightedRow = -1;
    for (size_t i = 0; i < details.size(); ++i) {
        const Product* product = service.findProduct(details[i].barcode);
        if (product == nullptr) {
            continue;
        }
        const double lineTotal =
            (product->unitPrice + product->taxPerUnit - product->discountPerUnit) *
            details[i].quantity;
        const int row = static_cast<int>(i);
        const QString barcode = QString::fromStdString(product->barcode);
        const bool justAdded = barcode == lastAddedBarcode;
        if (justAdded) {
            highlightedRow = row;
        }
        vector<QString> values = {
            QString::fromStdString(product->name),
            barcode,
            QString::number(details[i].quantity, 'f', 3),
            QString::fromStdString(moneyText(product->unitPrice)),
            QString::fromStdString(moneyText(lineTotal))
        };
        for (size_t column = 0; column < values.size(); ++column) {
            QTableWidgetItem* item = new QTableWidgetItem(values[column]);
            if (column >= 2) {
                item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            }
            if (justAdded) {
                item->setBackground(QColor("#e1f4e8"));
            }
            cartTable->setItem(row, static_cast<int>(column), item);
        }
    }
    if (highlightedRow >= 0) {
        cartTable->setCurrentCell(highlightedRow, 0);
        cartTable->scrollToItem(cartTable->item(highlightedRow, 0));
    }

    const OrderStatus status =
        static_cast<OrderStatus>(orderStatusInput->currentData().toInt());
    const BillSummary bill = service.calculateBill(
        customerNameInput->text().toStdString(), status);
    const QString itemLines = details.size() == 1
        ? "1 item line" : QString::number(details.size()) + " item lines";
    cartSummaryLabel->setText(
        itemLines + "   |   Total payable: PKR " +
        QString::fromStdString(moneyText(bill.totalPayable)));
    if (status == OrderStatus::Paid) {
        paymentAmountInput->setValue(bill.totalPayable);
    } else if (status == OrderStatus::InProgress) {
        paymentAmountInput->setValue(0);
    }

}

void MainWindow::setStatus(const QString& text, const QString& state) {
    statusText->setText(text);
    statusText->setProperty("state", state);
    statusText->style()->unpolish(statusText);
    statusText->style()->polish(statusText);
    statusText->update();
}

void MainWindow::updatePaymentFields() {
    if (paymentModeInput == nullptr) {
        return;
    }
    const PaymentMode mode =
        static_cast<PaymentMode>(paymentModeInput->currentData().toInt());
    merchantInput->setEnabled(mode != PaymentMode::Cash);
    if (mode == PaymentMode::Cash) {
        merchantInput->clear();
    }
    updateCart();
}

void MainWindow::updateOrderStatus() {
    const OrderStatus status =
        static_cast<OrderStatus>(orderStatusInput->currentData().toInt());
    if (status == OrderStatus::OnHold) {
        paymentAmountInput->setValue(0);
    }
    updateCart();
}

void MainWindow::updateRole() {
    const bool cashier = roleCombo->currentText() == "Cashier";
    if (cashier) {
        if (tabs->indexOf(orderPage) < 0) {
            tabs->addTab(orderPage, "Record order");
        }
    } else {
        const int orderIndex = tabs->indexOf(orderPage);
        if (orderIndex >= 0) {
            tabs->removeTab(orderIndex);
        }
    }
}

void MainWindow::completeOrder() {
    const OrderStatus status =
        static_cast<OrderStatus>(orderStatusInput->currentData().toInt());
    const PaymentMode mode =
        static_cast<PaymentMode>(paymentModeInput->currentData().toInt());
    Order order;
    BillSummary bill;
    string error;
    if (!service.completeOrder(
            customerNameInput->text().trimmed().toStdString(),
            customerContactInput->text().trimmed().toStdString(),
            mode,
            merchantInput->text().trimmed().toStdString(),
            paymentAmountInput->value(),
            status,
            order,
            bill,
            error)) {
        setStatus(QString::fromStdString(error), "error");
        QMessageBox::warning(this, "Could not generate bill",
                             QString::fromStdString(error));
        return;
    }

    showBill(order, bill);
    if (!error.empty()) {
        setStatus(QString::fromStdString(error), "warning");
        QMessageBox::warning(this, "Sale saved with a cleanup warning",
                             QString::fromStdString(error));
    } else {
        setStatus("Order and bill were saved successfully.", "success");
    }
    updateCart();
}

void MainWindow::showBill(const Order& order, const BillSummary& bill) {
    if (bill.lines.empty()) {
        QMessageBox::critical(this, "Bill unavailable",
                              "The order was saved, but there are no bill lines to display.");
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle("Sale receipt");
    dialog.resize(920, 600);
    QVBoxLayout* layout = new QVBoxLayout(&dialog);

    QLabel* heading = new QLabel("SALE RECEIPT", &dialog);
    heading->setObjectName("cartTitle");
    layout->addWidget(heading);

    QString details;
    details += "Bill ID: " + QString::fromStdString(bill.lines.front().billId) + "\n";
    details += "Order ID: " + QString::fromStdString(order.id) + "\n";
    details += "Date and time: " + QString::fromStdString(order.date) + " " +
               QString::fromStdString(order.time) + "\n";
    details += "Customer: " + QString::fromStdString(order.customerName) + "\n";
    details += "Contact: " + QString::fromStdString(order.customerContact);
    QLabel* detailLabel = new QLabel(details, &dialog);
    detailLabel->setTextFormat(Qt::PlainText);
    detailLabel->setObjectName("productInfoText");
    detailLabel->setWordWrap(true);
    layout->addWidget(detailLabel);

    QTableWidget* billTable = new QTableWidget(
        static_cast<int>(bill.lines.size()), 8, &dialog);
    billTable->setHorizontalHeaderLabels(
        {"Product ID", "Barcode", "Product", "Quantity", "Unit price",
         "Discount/unit", "Tax", "Payable"});
    billTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    billTable->horizontalHeader()->setStretchLastSection(true);
    billTable->verticalHeader()->setVisible(false);
    billTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    billTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    billTable->setAlternatingRowColors(true);
    billTable->setShowGrid(false);
    for (size_t row = 0; row < bill.lines.size(); ++row) {
        const BillLine& line = bill.lines[row];
        vector<QString> values = {
            QString::fromStdString(line.productId),
            QString::fromStdString(line.barcode),
            QString::fromStdString(line.productAcronym),
            QString::number(line.quantity, 'f', 3),
            QString::fromStdString(moneyText(line.unitPrice)),
            QString::fromStdString(moneyText(line.discountPerUnit)),
            QString::fromStdString(moneyText(line.totalTax)),
            QString::fromStdString(moneyText(line.totalPayable))
        };
        for (size_t column = 0; column < values.size(); ++column) {
            QTableWidgetItem* item = new QTableWidgetItem(values[column]);
            item->setTextAlignment(
                column < 3 ? Qt::AlignLeft | Qt::AlignVCenter
                           : Qt::AlignRight | Qt::AlignVCenter);
            billTable->setItem(
                static_cast<int>(row), static_cast<int>(column), item);
        }
    }
    layout->addWidget(billTable, 1);

    QGridLayout* totalsLayout = new QGridLayout();
    totalsLayout->addWidget(new QLabel("Subtotal before tax:", &dialog), 0, 0);
    totalsLayout->addWidget(new QLabel(
        "PKR " + QString::fromStdString(moneyText(bill.totalWithoutTax)), &dialog),
        0, 1, Qt::AlignRight);
    totalsLayout->addWidget(new QLabel("Total tax:", &dialog), 1, 0);
    totalsLayout->addWidget(new QLabel(
        "PKR " + QString::fromStdString(moneyText(bill.totalTax)), &dialog),
        1, 1, Qt::AlignRight);
    totalsLayout->addWidget(new QLabel("Total discount:", &dialog), 2, 0);
    totalsLayout->addWidget(new QLabel(
        "PKR " + QString::fromStdString(moneyText(bill.totalDiscount)), &dialog),
        2, 1, Qt::AlignRight);
    QLabel* payableLabel = new QLabel(
        "TOTAL PAYABLE: PKR " +
        QString::fromStdString(moneyText(bill.totalPayable)), &dialog);
    payableLabel->setObjectName("cartSummaryLabel");
    totalsLayout->addWidget(payableLabel, 3, 0, 1, 2, Qt::AlignRight);
    layout->addLayout(totalsLayout);

    QString paymentDetails;
    paymentDetails += "Payment received: PKR " +
                      QString::fromStdString(moneyText(order.paymentAmount));
    paymentDetails += "   |   " +
                      QString::fromStdString(paymentModeToString(order.paymentMode));
    paymentDetails += "   |   " +
                      QString::fromStdString(order.paymentMerchant);
    paymentDetails += "   |   " +
                      QString::fromStdString(orderStatusToString(order.status));
    QLabel* paymentLabel = new QLabel(paymentDetails, &dialog);
    paymentLabel->setTextFormat(Qt::PlainText);
    layout->addWidget(paymentLabel);

    QDialogButtonBox* buttons = new QDialogButtonBox(
        QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    layout->addWidget(buttons);
    dialog.exec();
}

#ifndef STORE_MODELS_H
#define STORE_MODELS_H

#include <string>
#include <vector>

enum class MeasurementUnit {
    Count,
    Weight
};

enum class OrderStatus {
    InProgress,
    OnHold,
    Paid,
    Archived
};

enum class PaymentMode {
    Cash,
    Card,
    Wallet
};

struct Product {
    std::string id;
    std::string barcode;
    std::string acronym;
    std::string name;
    MeasurementUnit unit = MeasurementUnit::Count;
    double unitPrice = 0;
    double taxPerUnit = 0;
    double discountPerUnit = 0;
    double availableQuantity = 0;
    std::string barcodePath;
};

struct Order {
    std::string id;
    std::string date;
    std::string time;
    std::string customerName;
    std::string customerContact;
    OrderStatus status = OrderStatus::InProgress;
    double paymentAmount = 0;
    PaymentMode paymentMode = PaymentMode::Cash;
    std::string paymentMerchant;
};

struct OrderDetail {
    std::string orderId;
    std::string productId;
    std::string barcode;
    double quantity = 0;
};

struct BillLine {
    std::string billId;
    std::string customerName;
    std::string orderId;
    std::string productId;
    std::string barcode;
    std::string productAcronym;
    double unitPrice = 0;
    double discountPerUnit = 0;
    double quantity = 0;
    double totalWithoutTax = 0;
    double totalTax = 0;
    double totalDiscount = 0;
    double totalPayable = 0;
    OrderStatus paymentStatus = OrderStatus::InProgress;
};

struct BillSummary {
    std::vector<BillLine> lines;
    double totalWithoutTax = 0;
    double totalTax = 0;
    double totalDiscount = 0;
    double totalPayable = 0;
};

std::string unitToString(MeasurementUnit unit);
std::string orderStatusToString(OrderStatus status);
std::string paymentModeToString(PaymentMode mode);

#endif

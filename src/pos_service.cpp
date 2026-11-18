#include "pos_service.h"

#include <cmath>
#include <ctime>
#include <iomanip>
#include <sstream>

using namespace std;

namespace {

double roundMoney(double amount) {
    return round(amount * 100.0) / 100.0;
}

bool validQuantity(const Product& product, double quantity) {
    if (!isfinite(quantity) || quantity <= 0 ||
        abs(quantity * 1000.0 - round(quantity * 1000.0)) > 0.000001) {
        return false;
    }
    if (product.unit == MeasurementUnit::Count &&
        floor(quantity) != quantity) {
        return false;
    }
    return true;
}

bool getLocalTime(tm& localTime) {
    const time_t currentTime = time(nullptr);
#ifdef _WIN32
    return localtime_s(&localTime, &currentTime) == 0;
#else
    return localtime_r(&currentTime, &localTime) != nullptr;
#endif
}

bool makeOrderDate(tm& localTime, string& date, string& timeText,
                   string& idPrefix) {
    if (!getLocalTime(localTime)) {
        return false;
    }
    char dateBuffer[11];
    char timeBuffer[9];
    char idBuffer[7];
    if (strftime(dateBuffer, sizeof(dateBuffer), "%Y-%m-%d", &localTime) == 0 ||
        strftime(timeBuffer, sizeof(timeBuffer), "%H:%M:%S", &localTime) == 0 ||
        strftime(idBuffer, sizeof(idBuffer), "%y%m%d", &localTime) == 0) {
        return false;
    }
    date = dateBuffer;
    timeText = timeBuffer;
    idPrefix = idBuffer;
    return true;
}

}  // namespace

PosService::PosService(StoreRepository& repository) : repository(repository) {}

bool PosService::loadProducts(string& error) {
    return repository.loadProducts(productList, error);
}

const vector<Product>& PosService::products() const {
    return productList;
}

const vector<OrderDetail>& PosService::cart() const {
    return orderDetails;
}

const Product* PosService::findProduct(const string& barcode) const {
    for (size_t i = 0; i < productList.size(); ++i) {
        if (productList[i].barcode == barcode) {
            return &productList[i];
        }
    }
    return nullptr;
}

bool PosService::addToCart(const string& barcode, double quantity, string& error) {
    const Product* product = findProduct(barcode);
    if (product == nullptr) {
        error = "No product has barcode " + barcode + ".";
        return false;
    }
    if (!validQuantity(*product, quantity)) {
        error = product->unit == MeasurementUnit::Count
              ? "Enter a positive whole-number quantity for this product."
              : "Enter a positive quantity with at most 3 decimal places.";
        return false;
    }

    double quantityInCart = 0;
    for (const OrderDetail& detail : orderDetails) {
        if (detail.barcode == barcode) {
            quantityInCart += detail.quantity;
        }
    }
    if (quantityInCart + quantity > product->availableQuantity + 0.000001) {
        error = "The requested quantity exceeds the available stock.";
        return false;
    }

    for (OrderDetail& detail : orderDetails) {
        if (detail.barcode == barcode) {
            detail.quantity += quantity;
            error.clear();
            return true;
        }
    }

    OrderDetail detail;
    detail.productId = product->id;
    detail.barcode = product->barcode;
    detail.quantity = quantity;
    orderDetails.push_back(detail);
    error.clear();
    return true;
}

bool PosService::removeFromCart(const string& barcode) {
    for (size_t i = 0; i < orderDetails.size(); ++i) {
        if (orderDetails[i].barcode == barcode) {
            orderDetails.erase(orderDetails.begin() + i);
            return true;
        }
    }
    return false;
}

BillSummary PosService::calculateBill(const string& customerName,
                                     OrderStatus status) const {
    BillSummary bill;
    for (const OrderDetail& detail : orderDetails) {
        const Product* product = findProduct(detail.barcode);
        if (product == nullptr) {
            continue;
        }

        BillLine line;
        line.customerName = customerName;
        line.orderId = "";
        line.productId = product->id;
        line.barcode = product->barcode;
        line.productAcronym = product->acronym;
        line.unitPrice = product->unitPrice;
        line.discountPerUnit = product->discountPerUnit;
        line.quantity = detail.quantity;
        line.totalWithoutTax = roundMoney(product->unitPrice * detail.quantity);
        line.totalTax = roundMoney(product->taxPerUnit * detail.quantity);
        line.totalDiscount = roundMoney(product->discountPerUnit * detail.quantity);
        line.totalPayable = roundMoney(
            line.totalWithoutTax + line.totalTax - line.totalDiscount);
        line.paymentStatus = status;

        bill.totalWithoutTax += line.totalWithoutTax;
        bill.totalTax += line.totalTax;
        bill.totalDiscount += line.totalDiscount;
        bill.totalPayable += line.totalPayable;
        bill.lines.push_back(line);
    }

    bill.totalWithoutTax = roundMoney(bill.totalWithoutTax);
    bill.totalTax = roundMoney(bill.totalTax);
    bill.totalDiscount = roundMoney(bill.totalDiscount);
    bill.totalPayable = roundMoney(bill.totalPayable);
    return bill;
}

bool PosService::completeOrder(const string& customerName,
                               const string& customerContact,
                               PaymentMode paymentMode,
                               const string& paymentMerchant,
                               double paymentAmount,
                               OrderStatus status,
                               Order& savedOrder,
                               BillSummary& savedBill,
                               string& error) {
    if (orderDetails.empty()) {
        error = "Add at least one product before generating a bill.";
        return false;
    }
    if (customerName.empty()) {
        error = "Customer name cannot be empty.";
        return false;
    }
    if (!isfinite(paymentAmount) || paymentAmount < 0) {
        error = "Payment amount must be a non-negative number.";
        return false;
    }
    if (abs(paymentAmount * 100 - round(paymentAmount * 100)) > 0.000001) {
        error = "Payment amount can have at most 2 decimal places.";
        return false;
    }
    if ((paymentMode == PaymentMode::Card || paymentMode == PaymentMode::Wallet) &&
        paymentMerchant.empty()) {
        error = "Enter the payment merchant for card or wallet payments.";
        return false;
    }

    BillSummary bill = calculateBill(customerName, status);
    if (paymentAmount > bill.totalPayable) {
        error = "Payment amount cannot be greater than the total bill.";
        return false;
    }
    if (status == OrderStatus::Paid && paymentAmount != bill.totalPayable) {
        error = "A paid order must be paid in full.";
        return false;
    }
    if (status == OrderStatus::InProgress && paymentAmount != 0) {
        error = "An in-progress order cannot have a recorded payment.";
        return false;
    }
    if (status == OrderStatus::OnHold && paymentAmount >= bill.totalPayable) {
        error = "An on-hold order must have an outstanding balance.";
        return false;
    }

    tm localTime{};
    string date;
    string timeText;
    string idPrefix;
    if (!makeOrderDate(localTime, date, timeText, idPrefix)) {
        error = "Could not read the current local date and time.";
        return false;
    }

    Order order;
    if (!repository.nextOrderId(idPrefix, order.id, error) ||
        !repository.nextBillId(bill.lines.front().billId, error)) {
        return false;
    }
    order.date = date;
    order.time = timeText;
    order.customerName = customerName;
    order.customerContact = customerContact.empty() ? "-" : customerContact;
    order.status = status;
    order.paymentAmount = roundMoney(paymentAmount);
    order.paymentMode = paymentMode;
    order.paymentMerchant = paymentMode == PaymentMode::Cash ? "-" : paymentMerchant;

    vector<OrderDetail> details = orderDetails;
    vector<Product> updatedProducts = productList;
    for (OrderDetail& detail : details) {
        detail.orderId = order.id;
        for (Product& product : updatedProducts) {
            if (product.barcode == detail.barcode) {
                product.availableQuantity -= detail.quantity;
                if (abs(product.availableQuantity) < 0.000001) {
                    product.availableQuantity = 0;
                }
                break;
            }
        }
    }

    for (BillLine& line : bill.lines) {
        line.billId = bill.lines.front().billId;
        line.customerName = customerName;
        line.orderId = order.id;
        line.paymentStatus = status;
    }

    if (!repository.saveSale(updatedProducts, order, details, bill.lines, error)) {
        return false;
    }

    productList = updatedProducts;
    orderDetails.clear();
    savedOrder = order;
    savedBill = bill;
    return true;
}

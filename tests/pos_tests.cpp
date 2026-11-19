#include "pos_service.h"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

using namespace std;

namespace {

bool check(bool condition, const string& description) {
    if (!condition) {
        cerr << "FAILED: " << description << '\n';
        return false;
    }
    return true;
}

bool closeTo(double actual, double expected) {
    return abs(actual - expected) < 0.001;
}

}  // namespace

int main() {
    bool passed = true;
    const auto uniquePart = chrono::steady_clock::now().time_since_epoch().count();
    const filesystem::path folder =
        filesystem::temp_directory_path() /
        ("store_pos_test_" + to_string(uniquePart));
    filesystem::create_directories(folder);

    {
        ofstream products(folder / "Product.txt");
        products << "1 123456 TEA \"Tea Bag\" count 100.00 10.00 5.00 "
                    "2 \"barcodes/tea bag.bmp\" #\n";
        products << "2 234567 RICE \"Rice Bag\" weight 1000.00 50.00 10.00 "
                    "5.500 \"barcodes/rice bag.bmp\" #\n";
    }

    StoreRepository repository(folder);
    PosService service(repository);
    string error;
    passed &= check(service.loadProducts(error), "quoted product rows load");
    passed &= check(service.products().size() == 2, "both products are loaded");
    if (service.products().size() == 2) {
        passed &= check(service.products()[0].name == "Tea Bag",
                        "quoted product name is preserved");
        passed &= check(service.products()[1].barcodePath == "barcodes/rice bag.bmp",
                        "quoted barcode path is preserved");
    }

    passed &= check(!service.addToCart("123456", 0.5, error),
                    "count products reject fractional quantities");
    passed &= check(service.addToCart("123456", 1, error),
                    "count product can be added");
    passed &= check(!service.addToCart("123456", 2, error),
                    "cart quantity cannot exceed available stock");
    passed &= check(service.addToCart("234567", 1.25, error),
                    "weight product accepts three-decimal quantities");

    const BillSummary preview =
        service.calculateBill("Test Customer", OrderStatus::Paid);
    passed &= check(closeTo(preview.totalWithoutTax, 1350.00),
                    "bill subtotal is calculated");
    passed &= check(closeTo(preview.totalTax, 72.50),
                    "per-unit tax is multiplied by quantity");
    passed &= check(closeTo(preview.totalDiscount, 17.50),
                    "per-unit discount is multiplied by quantity");
    passed &= check(closeTo(preview.totalPayable, 1405.00),
                    "payable total includes tax and discount");

    Order firstOrder;
    BillSummary firstBill;
    passed &= check(service.completeOrder(
                        "Test Customer", "", PaymentMode::Cash, "", 1405.00,
                        OrderStatus::Paid, firstOrder, firstBill, error),
                    "paid multi-product order is saved");
    passed &= check(firstOrder.id.size() == 8 &&
                    firstOrder.id.substr(6) == "01",
                    "first daily order ID has the expected format");
    passed &= check(firstBill.lines.size() == 2,
                    "bill has one row for each product");
    if (firstBill.lines.size() == 2) {
        passed &= check(firstBill.lines[0].billId == "100000000" &&
                        firstBill.lines[1].billId == "100000000",
                        "bill rows share a nine-digit bill ID");
    }

    vector<Product> reloadedProducts;
    passed &= check(repository.loadProducts(reloadedProducts, error),
                    "updated products reload after saving the order");
    if (reloadedProducts.size() == 2) {
        passed &= check(closeTo(reloadedProducts[0].availableQuantity, 1),
                        "count stock is reduced after sale");
        passed &= check(closeTo(reloadedProducts[1].availableQuantity, 4.25),
                        "weight stock is reduced after sale");
    } else {
        passed &= check(false, "two updated products remain in Product.txt");
    }

    passed &= check(service.addToCart("123456", 1, error),
                    "remaining count stock can be sold");
    Order secondOrder;
    BillSummary secondBill;
    passed &= check(service.completeOrder(
                        "Another Customer", "03000000000", PaymentMode::Wallet,
                        "JazzCash", 0, OrderStatus::OnHold,
                        secondOrder, secondBill, error),
                    "on-hold order with a wallet merchant is saved");
    passed &= check(secondOrder.id.substr(6) == "02",
                    "daily order sequence increments");
    if (!secondBill.lines.empty()) {
        passed &= check(secondBill.lines[0].billId == "100000001",
                        "bill ID increments after the first sale");
    }

    vector<string> orderRows;
    vector<string> detailRows;
    vector<string> billRows;
    const char* salesFiles[] = {"Order.txt", "OrderDetail.txt", "Bill.txt"};
    vector<string>* rowLists[] = {&orderRows, &detailRows, &billRows};
    for (size_t fileIndex = 0; fileIndex < 3; ++fileIndex) {
        ifstream input(folder / salesFiles[fileIndex]);
        string row;
        while (getline(input, row)) {
            rowLists[fileIndex]->push_back(row);
        }
        passed &= check(!input.bad(),
                        string(salesFiles[fileIndex]) + " can be read after saving");
    }
    passed &= check(orderRows.size() == 2, "one order header is saved per order");
    passed &= check(detailRows.size() == 3,
                    "each product in an order gets a detail row");
    passed &= check(billRows.size() == 3,
                    "each product in a bill gets a bill row");
    if (orderRows.size() == 2) {
        passed &= check(orderRows[0].find("\"Test Customer\"") != string::npos &&
                        orderRows[0].back() == '#',
                        "order row quotes names and ends with #");
    }
    if (billRows.size() == 3) {
        passed &= check(billRows[0].back() == '#' && billRows[1].back() == '#',
                        "every bill product row ends with #");
    }

    const char* outputFiles[] = {
        "Product.txt", "Order.txt", "OrderDetail.txt", "Bill.txt"
    };
    for (size_t i = 0; i < sizeof(outputFiles) / sizeof(outputFiles[0]); ++i) {
        const string fileName = outputFiles[i];
        passed &= check(filesystem::exists(folder / fileName),
                        fileName + " is created");
    }

    error_code removeError;
    filesystem::remove_all(folder, removeError);
    passed &= check(!removeError, "temporary POS files are removed");
    return passed ? 0 : 1;
}

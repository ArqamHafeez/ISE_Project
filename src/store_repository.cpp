#include "store_repository.h"

#include "barcode.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <utility>

using namespace std;
namespace fs = std::filesystem;

namespace {

bool parseNumber(const string& text, double& value) {
    istringstream input(text);
    input >> value;
    if (!input || !isfinite(value)) {
        return false;
    }

    char extra;
    return !(input >> extra);
}

bool validProduct(const Product& product, string& error) {
    if (product.id.empty() || product.acronym.empty() ||
        product.acronym.size() > 8 || product.name.empty() ||
        product.barcodePath.empty()) {
        error = "Product ID, acronym, name, and barcode path must not be empty; "
                "the acronym can contain at most 8 characters.";
        return false;
    }
    if (!isValidBarcodeNumber(product.barcode)) {
        error = "Product barcode must contain 6 digits and cannot start with zero.";
        return false;
    }
    if (product.unitPrice < 0 || product.taxPerUnit < 0 ||
        product.discountPerUnit < 0 || product.availableQuantity < 0) {
        error = "Product prices, tax, discount, and quantity cannot be negative.";
        return false;
    }
    if (abs(product.unitPrice * 100 - round(product.unitPrice * 100)) > 0.000001 ||
        abs(product.taxPerUnit * 100 - round(product.taxPerUnit * 100)) > 0.000001 ||
        abs(product.discountPerUnit * 100 -
            round(product.discountPerUnit * 100)) > 0.000001) {
        error = "Product prices, tax, and discount can have at most 2 decimal places.";
        return false;
    }
    if (product.discountPerUnit > product.unitPrice) {
        error = "A product discount cannot be greater than its unit price.";
        return false;
    }
    if (product.unit == MeasurementUnit::Count &&
        floor(product.availableQuantity) != product.availableQuantity) {
        error = "The available quantity of a count-based product must be whole.";
        return false;
    }
    if (product.unit == MeasurementUnit::Weight &&
        abs(product.availableQuantity * 1000 -
            round(product.availableQuantity * 1000)) > 0.000001) {
        error = "A weight-based product quantity can have at most 3 decimal places.";
        return false;
    }
    return true;
}

bool readProductRow(const string& row, Product& product, string& error) {
    istringstream input(row);
    string unit;
    string price;
    string tax;
    string discount;
    string quantity;
    string endMarker;

    if (!(input >> product.id >> product.barcode >> product.acronym
          >> quoted(product.name) >> unit >> price >> tax >> discount
          >> quantity >> quoted(product.barcodePath) >> endMarker)) {
        error = "The row does not contain all required product fields.";
        return false;
    }

    string extra;
    if (endMarker != "#" || input >> extra) {
        error = "The product row must end with # and contain no extra fields.";
        return false;
    }
    if (unit == "count") {
        product.unit = MeasurementUnit::Count;
    } else if (unit == "weight") {
        product.unit = MeasurementUnit::Weight;
    } else {
        error = "Measurement unit must be count or weight.";
        return false;
    }
    if (!parseNumber(price, product.unitPrice) ||
        !parseNumber(tax, product.taxPerUnit) ||
        !parseNumber(discount, product.discountPerUnit) ||
        !parseNumber(quantity, product.availableQuantity)) {
        error = "Price, tax, discount, and quantity must be valid numbers.";
        return false;
    }
    return validProduct(product, error);
}

string formatMoney(double amount) {
    ostringstream output;
    output << fixed << setprecision(2) << amount;
    return output.str();
}

string formatQuantity(double quantity) {
    ostringstream output;
    output << fixed << setprecision(3) << quantity;
    return output.str();
}

string productRow(const Product& product) {
    ostringstream output;
    output << product.id << ' ' << product.barcode << ' ' << product.acronym << ' '
           << quoted(product.name) << ' ' << unitToString(product.unit) << ' '
           << formatMoney(product.unitPrice) << ' '
           << formatMoney(product.taxPerUnit) << ' '
           << formatMoney(product.discountPerUnit) << ' '
           << formatQuantity(product.availableQuantity) << ' '
           << quoted(product.barcodePath) << " #";
    return output.str();
}

string orderRow(const Order& order) {
    ostringstream output;
    output << order.id << ' ' << order.date << ' ' << order.time << ' '
           << quoted(order.customerName) << ' '
           << quoted(order.customerContact) << ' '
           << orderStatusToString(order.status) << ' '
           << formatMoney(order.paymentAmount) << ' '
           << paymentModeToString(order.paymentMode) << ' '
           << quoted(order.paymentMerchant) << " #";
    return output.str();
}

string detailRow(const OrderDetail& detail) {
    ostringstream output;
    output << detail.orderId << ' ' << detail.productId << ' '
           << detail.barcode << ' ' << formatQuantity(detail.quantity);
    return output.str();
}

string billRow(const BillLine& bill) {
    ostringstream output;
    output << bill.billId << ' ' << quoted(bill.customerName) << ' '
           << bill.orderId << ' ' << bill.productAcronym << ' '
           << formatMoney(bill.unitPrice) << ' '
           << formatMoney(bill.discountPerUnit) << ' '
           << formatQuantity(bill.quantity) << ' '
           << formatMoney(bill.totalWithoutTax) << ' '
           << formatMoney(bill.totalTax) << ' '
           << formatMoney(bill.totalDiscount) << ' '
           << formatMoney(bill.totalPayable) << ' '
           << orderStatusToString(bill.paymentStatus) << " #";
    return output.str();
}

bool readFile(const fs::path& path, string& contents, string& error) {
    ifstream input(path, ios::binary);
    if (!input) {
        if (fs::exists(path)) {
            error = "Could not read " + path.string() + ".";
            return false;
        }
        error.clear();
        contents.clear();
        return true;
    }
    contents.assign(istreambuf_iterator<char>(input), istreambuf_iterator<char>());
    if (input.bad()) {
        error = "Could not read " + path.string() + ".";
        return false;
    }
    return true;
}

void addRow(string& contents, const string& row) {
    if (!contents.empty() && contents.back() != '\n') {
        contents += '\n';
    }
    contents += row;
    contents += '\n';
}

struct StagedFile {
    fs::path target;
    fs::path temporary;
    fs::path backup;
    bool originalMoved = false;
    bool newFileInstalled = false;
};

void recordFileError(string& error, const string& message) {
    if (!error.empty()) {
        error += ' ';
    }
    error += message;
}

void rollback(vector<StagedFile>& files, string& error) {
    for (size_t i = files.size(); i > 0; --i) {
        StagedFile& file = files[i - 1];
        error_code fileError;
        if (file.newFileInstalled) {
            fs::remove(file.target, fileError);
            if (fileError) {
                recordFileError(error, "Rollback could not remove " +
                                file.target.string() + ".");
            }
        }
        if (file.originalMoved) {
            fileError.clear();
            fs::rename(file.backup, file.target, fileError);
            if (fileError) {
                recordFileError(error, "Rollback could not restore " +
                                file.target.string() + ".");
            }
        }
        fileError.clear();
        fs::remove(file.temporary, fileError);
        if (fileError) {
            recordFileError(error, "Rollback could not remove temporary file " +
                            file.temporary.string() + ".");
        }
    }
}

bool stageFile(StagedFile& file, const string& contents, string& error) {
    ofstream output(file.temporary, ios::binary | ios::trunc);
    if (!output) {
        error = "Could not create temporary file " + file.temporary.string() + ".";
        return false;
    }
    output.write(contents.data(), static_cast<streamsize>(contents.size()));
    output.close();
    if (!output) {
        error = "Could not write temporary file " + file.temporary.string() + ".";
        return false;
    }
    return true;
}

bool commitFiles(vector<StagedFile>& files, string& error) {
    for (StagedFile& file : files) {
        error_code fileError;
        if (fs::exists(file.target, fileError)) {
            fs::rename(file.target, file.backup, fileError);
            if (fileError) {
                error = "Could not back up " + file.target.string() + ".";
                rollback(files, error);
                return false;
            }
            file.originalMoved = true;
        } else if (fileError) {
            error = "Could not inspect " + file.target.string() + ".";
            rollback(files, error);
            return false;
        }

        fs::rename(file.temporary, file.target, fileError);
        if (fileError) {
            error = "Could not replace " + file.target.string() + ".";
            rollback(files, error);
            return false;
        }
        file.newFileInstalled = true;
    }

    for (const StagedFile& file : files) {
        if (file.originalMoved) {
            error_code fileError;
            fs::remove(file.backup, fileError);
            if (fileError) {
                recordFileError(error, "Sale was saved, but could not remove backup file " +
                                file.backup.string() + ".");
            }
        }
    }
    return true;
}

}  // namespace

StoreRepository::StoreRepository(const fs::path& folder) : folder(folder) {}

bool StoreRepository::loadProducts(vector<Product>& products, string& error) const {
    const fs::path path = folder / "Product.txt";
    ifstream input(path);
    if (!input) {
        error = "Could not open " + path.string() + ".";
        return false;
    }

    vector<Product> loadedProducts;
    vector<string> barcodes;
    vector<string> productIds;
    string row;
    size_t rowNumber = 0;
    while (getline(input, row)) {
        ++rowNumber;
        if (row.empty()) {
            error = "Product.txt contains an empty row at line " +
                    to_string(rowNumber) + ".";
            return false;
        }

        Product product;
        if (!readProductRow(row, product, error)) {
            error = "Product.txt line " + to_string(rowNumber) + ": " + error;
            return false;
        }
        for (size_t i = 0; i < loadedProducts.size(); ++i) {
            if (product.id == productIds[i] || product.barcode == barcodes[i]) {
                error = "Product.txt contains a duplicate product ID or barcode at line " +
                        to_string(rowNumber) + ".";
                return false;
            }
        }
        productIds.push_back(product.id);
        barcodes.push_back(product.barcode);
        loadedProducts.push_back(product);
    }

    if (input.bad()) {
        error = "An error occurred while reading " + path.string() + ".";
        return false;
    }
    products = move(loadedProducts);
    error.clear();
    return true;
}

bool StoreRepository::nextOrderId(const string& datePrefix, string& id,
                                  string& error) const {
    ifstream input(folder / "Order.txt");
    if (!input && fs::exists(folder / "Order.txt")) {
        error = "Could not read Order.txt.";
        return false;
    }

    int lastSequence = 0;
    string row;
    while (getline(input, row)) {
        istringstream line(row);
        string existingId;
        if (!(line >> existingId) || existingId.size() != 8) {
            error = "Order.txt contains an invalid order ID.";
            return false;
        }
        if (existingId.compare(0, 6, datePrefix) == 0) {
            const string sequence = existingId.substr(6, 2);
            if (!isdigit(static_cast<unsigned char>(sequence[0])) ||
                !isdigit(static_cast<unsigned char>(sequence[1]))) {
                error = "Order.txt contains an invalid order sequence.";
                return false;
            }
            lastSequence = max(lastSequence, stoi(sequence));
        }
        for (size_t i = 0; i < existingId.size(); ++i) {
            if (!isdigit(static_cast<unsigned char>(existingId[i]))) {
                error = "Order.txt contains a non-numeric order ID.";
                return false;
            }
        }
    }
    if (lastSequence >= 99) {
        error = "The daily order ID limit of 99 has been reached.";
        return false;
    }

    ostringstream nextId;
    nextId << datePrefix << setw(2) << setfill('0') << lastSequence + 1;
    id = nextId.str();
    error.clear();
    return true;
}

bool StoreRepository::nextBillId(string& id, string& error) const {
    ifstream input(folder / "Bill.txt");
    if (!input && fs::exists(folder / "Bill.txt")) {
        error = "Could not read Bill.txt.";
        return false;
    }

    unsigned long long largestId = 99999999;
    string row;
    while (getline(input, row)) {
        istringstream line(row);
        string existingId;
        if (!(line >> existingId) || existingId.size() != 9) {
            error = "Bill.txt contains an invalid bill ID.";
            return false;
        }
        for (size_t i = 0; i < existingId.size(); ++i) {
            if (!isdigit(static_cast<unsigned char>(existingId[i]))) {
                error = "Bill.txt contains a non-numeric bill ID.";
                return false;
            }
        }
        largestId = max(largestId, stoull(existingId));
    }
    if (largestId >= 999999999) {
        error = "The available nine-digit bill IDs have been used.";
        return false;
    }

    id = to_string(largestId + 1);
    error.clear();
    return true;
}

bool StoreRepository::saveSale(const vector<Product>& updatedProducts,
                               const Order& order,
                               const vector<OrderDetail>& details,
                               const vector<BillLine>& billLines,
                               string& error) const {
    if (details.empty() || billLines.empty() ||
        details.size() != billLines.size()) {
        error = "A sale must contain matching order details and bill lines.";
        return false;
    }

    vector<StagedFile> files;
    const auto makeFile = [&](const string& name, const string& contents) {
        const auto uniqueId = chrono::steady_clock::now().time_since_epoch().count()
                            + static_cast<long long>(files.size());
        StagedFile file;
        file.target = folder / name;
        const string suffix = ".tmp." + to_string(uniqueId);
        file.temporary = file.target.string() + suffix;
        file.backup = file.target.string() + ".bak" + suffix;
        files.push_back(file);
        return stageFile(files.back(), contents, error);
    };

    ostringstream productsText;
    for (const Product& product : updatedProducts) {
        string productError;
        if (!validProduct(product, productError)) {
            error = "Cannot save product " + product.id + ": " + productError;
            return false;
        }
        productsText << productRow(product) << '\n';
    }

    string ordersText;
    string detailsText;
    string billsText;
    if (!readFile(folder / "Order.txt", ordersText, error) ||
        !readFile(folder / "OrderDetail.txt", detailsText, error) ||
        !readFile(folder / "Bill.txt", billsText, error)) {
        return false;
    }

    addRow(ordersText, orderRow(order));
    for (const OrderDetail& detail : details) {
        addRow(detailsText, detailRow(detail));
    }
    for (const BillLine& bill : billLines) {
        addRow(billsText, billRow(bill));
    }

    if (!makeFile("Product.txt", productsText.str()) ||
        !makeFile("Order.txt", ordersText) ||
        !makeFile("OrderDetail.txt", detailsText) ||
        !makeFile("Bill.txt", billsText)) {
        rollback(files, error);
        return false;
    }
    if (!commitFiles(files, error)) {
        return false;
    }
    error.clear();
    return true;
}

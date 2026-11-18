#ifndef POS_SERVICE_H
#define POS_SERVICE_H

#include "models.h"
#include "store_repository.h"

#include <string>
#include <vector>

class PosService {
public:
    explicit PosService(StoreRepository& repository);

    bool loadProducts(std::string& error);
    const std::vector<Product>& products() const;
    const std::vector<OrderDetail>& cart() const;
    const Product* findProduct(const std::string& barcode) const;

    bool addToCart(const std::string& barcode, double quantity,
                   std::string& error);
    bool removeFromCart(const std::string& barcode);
    BillSummary calculateBill(const std::string& customerName,
                              OrderStatus status) const;
    bool completeOrder(const std::string& customerName,
                       const std::string& customerContact,
                       PaymentMode paymentMode,
                       const std::string& paymentMerchant,
                       double paymentAmount,
                       OrderStatus status,
                       Order& savedOrder,
                       BillSummary& savedBill,
                       std::string& error);

private:
    StoreRepository& repository;
    std::vector<Product> productList;
    std::vector<OrderDetail> orderDetails;
};

#endif

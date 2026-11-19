#ifndef STORE_REPOSITORY_H
#define STORE_REPOSITORY_H

#include "models.h"

#include <filesystem>
#include <string>
#include <vector>

class StoreRepository {
public:
    explicit StoreRepository(const std::filesystem::path& folder);

    bool loadProducts(std::vector<Product>& products, std::string& error) const;
    bool nextOrderId(const std::string& datePrefix, std::string& id,
                     std::string& error) const;
    bool nextBillId(std::string& id, std::string& error) const;
    bool saveSale(const std::vector<Product>& updatedProducts,
                  const Order& order,
                  const std::vector<OrderDetail>& details,
                  const std::vector<BillLine>& billLines,
                  std::string& error) const;

private:
    std::filesystem::path folder;
};

#endif

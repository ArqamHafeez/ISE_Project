#include "models.h"

std::string unitToString(MeasurementUnit unit) {
    if (unit == MeasurementUnit::Weight) {
        return "weight";
    }
    return "count";
}

std::string orderStatusToString(OrderStatus status) {
    if (status == OrderStatus::OnHold) {
        return "on-hold";
    }
    if (status == OrderStatus::Paid) {
        return "paid";
    }
    if (status == OrderStatus::Archived) {
        return "archived";
    }
    return "in-progress";
}

std::string paymentModeToString(PaymentMode mode) {
    if (mode == PaymentMode::Card) {
        return "card";
    }
    if (mode == PaymentMode::Wallet) {
        return "wallet";
    }
    return "cash";
}

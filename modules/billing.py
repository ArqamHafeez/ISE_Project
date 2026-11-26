"""Bill calculation, persistence, and receipts."""
from __future__ import annotations

from datetime import datetime
from pathlib import Path
from typing import Dict, List, Optional

from .file_io import RECEIPTS_DIR, append_line, display_name, read_lines, space_name, write_lines
from .products import get_by_id


SHOP_NAME = "Zain Mart POS"


def make_bill_id() -> str:
    ids = []
    for line in read_lines("Bill.txt"):
        parts = line.split()
        if parts and parts[0].isdigit():
            ids.append(int(parts[0]))
    n = (max(ids) + 1) if ids else 100000001
    return str(n).zfill(9)


def line_totals(unit_price: float, unit_discount: float, tax: float, qty: float) -> Dict[str, float]:
    sub = (unit_price - unit_discount) * qty
    tax_t = tax * qty
    disc_t = unit_discount * qty
    return {
        "subtotal": round(sub, 2),
        "tax": round(tax_t, 2),
        "discount": round(disc_t, 2),
        "line_payable": round(sub + tax_t, 2),
    }


def calculate_bill(items: List[Dict], bill_discount: float = 0.0, seasonal_discount: float = 0.0) -> Dict:
    """
    items: each needs unit_price, unit_discount, tax, quantity, acronym (optional)
    Payable = Σ((unit price − unit discount) × qty) + Σ(tax × qty) − bill-level discount
    """
    total_without_tax = 0.0
    total_tax = 0.0
    total_unit_discount = 0.0
    enriched = []
    for it in items:
        lt = line_totals(it["unit_price"], it["unit_discount"], it["tax"], it["quantity"])
        total_without_tax += lt["subtotal"]
        total_tax += lt["tax"]
        total_unit_discount += lt["discount"]
        enriched.append({**it, **lt})
    bill_discount = float(bill_discount or 0)
    before_bill_discounts = total_without_tax + total_tax
    seasonal_discount = min(max(float(seasonal_discount or 0), 0), before_bill_discounts)
    bill_discount = min(max(float(bill_discount or 0), 0), before_bill_discounts - seasonal_discount)
    payable = before_bill_discounts - seasonal_discount - bill_discount
    return {
        "items": enriched,
        "total_without_tax": round(total_without_tax, 2),
        "total_tax": round(total_tax, 2),
        "total_discount": round(total_unit_discount, 2),
        "all_discount": round(total_unit_discount + bill_discount + seasonal_discount, 2),
        "unit_discount_total": round(total_unit_discount, 2),
        "bill_discount": round(bill_discount, 2),
        "seasonal_discount": round(seasonal_discount, 2),
        "special_discount": round(bill_discount, 2),
        "payable": round(max(payable, 0), 2),
    }


def bill_to_line(bill: Dict) -> str:
    return (
        f"{bill['bill_id']} {space_name(bill['customer'])} {bill['order_id']} "
        f"{bill['total_without_tax']} {bill['total_tax']} {bill['total_discount']} "
        f"{bill['payable']} {bill.get('amount_received', 0)} {bill.get('balance_returned', 0)} "
        f"{bill['status']} {bill.get('seasonal_discount', 0)} {bill.get('special_discount', 0)} "
        f"{bill.get('date', datetime.now().strftime('%Y-%m-%d'))} "
        f"{bill.get('time', datetime.now().strftime('%H:%M:%S'))}"
    )


def parse_bill(line: str) -> Optional[Dict]:
    parts = line.split()
    if len(parts) >= 14:
        return {
            "bill_id": parts[0],
            "customer": display_name(parts[1]),
            "order_id": parts[2],
            "items": [],
            "total_without_tax": float(parts[3]),
            "total_tax": float(parts[4]),
            "total_discount": float(parts[5]),
            "payable": float(parts[6]),
            "amount_received": float(parts[7]),
            "balance_returned": float(parts[8]),
            "status": parts[9],
            "seasonal_discount": float(parts[10]),
            "special_discount": float(parts[11]),
            "date": parts[12],
            "time": parts[13],
        }
    if len(parts) < 9:
        return None
    # last 5 fields are totals + status
    status = parts[-1]
    payable = float(parts[-2])
    total_discount = float(parts[-3])
    total_tax = float(parts[-4])
    total_without_tax = float(parts[-5])
    bill_id = parts[0]
    customer = display_name(parts[1])
    order_id = parts[2]
    item_field = " ".join(parts[3:-5])  # normally no spaces
    # item_field is semicolon groups
    items = []
    for group in item_field.split(";"):
        bits = group.split(",")
        if len(bits) == 4:
            items.append({
                "acronym": bits[0],
                "unit_price": float(bits[1]),
                "unit_discount": float(bits[2]),
                "quantity": float(bits[3]),
            })
    return {
        "bill_id": bill_id,
        "customer": customer,
        "order_id": order_id,
        "items": items,
        "total_without_tax": total_without_tax,
        "total_tax": total_tax,
        "total_discount": total_discount,
        "payable": payable,
        "status": status,
        "amount_received": payable,
        "balance_returned": 0.0,
        "seasonal_discount": 0.0,
        "special_discount": 0.0,
        "date": "",
        "time": "",
    }


def list_bill_details() -> Dict[str, List[Dict]]:
    grouped: Dict[str, List[Dict]] = {}
    for line in read_lines("BillDetail.txt"):
        parts = line.split()
        if len(parts) != 5:
            continue
        try:
            detail = {
                "bill_id": parts[0],
                "acronym": parts[1],
                "unit_price": float(parts[2]),
                "quantity": float(parts[3]),
                "discount": float(parts[4]),
                "unit_discount": float(parts[4]) / float(parts[3]) if float(parts[3]) else 0,
            }
        except ValueError:
            continue
        grouped.setdefault(parts[0], []).append(detail)
    return grouped


def list_bills() -> List[Dict]:
    detail_rows = list_bill_details()
    out = []
    for line in read_lines("Bill.txt"):
        b = parse_bill(line)
        if b:
            if detail_rows.get(b["bill_id"]):
                b["items"] = detail_rows[b["bill_id"]]
            else:
                # Legacy Bill.txt lines embedded their item details.
                for item in b["items"]:
                    item["discount"] = round(item["unit_discount"] * item["quantity"], 2)
            out.append(b)
    return out


def migrate_bills_file() -> None:
    """Convert embedded-item legacy bills to the Bill.txt + BillDetail.txt layout."""
    raw_lines = read_lines("Bill.txt")
    if not raw_lines or all(len(line.split()) >= 14 for line in raw_lines):
        return
    bills = list_bills()
    from .orders import list_orders
    order_map = {order["order_id"]: order for order in list_orders()}
    for bill in bills:
        order = order_map.get(bill["order_id"], {})
        product_discounts = sum(
            float(item.get("unit_discount", 0)) * float(item["quantity"])
            for item in bill.get("items", [])
        )
        bill["special_discount"] = max(0.0, bill["total_discount"] - product_discounts)
        bill["total_discount"] = round(product_discounts, 2)
        bill["seasonal_discount"] = 0.0
        bill["amount_received"] = bill["payable"] if bill["status"] == "paid" else 0.0
        bill["balance_returned"] = 0.0
        bill["date"] = order.get("date", datetime.now().strftime("%Y-%m-%d"))
        bill["time"] = order.get("time", "00:00:00")
    write_lines("Bill.txt", [bill_to_line(bill) for bill in bills])
    _write_bill_details(bills)


def get_bill_by_order(order_id: str) -> Optional[Dict]:
    for b in list_bills():
        if b["order_id"] == order_id:
            return b
    return None


def get_bill_by_id(bill_id: str) -> Optional[Dict]:
    return next((bill for bill in list_bills() if bill["bill_id"] == bill_id), None)


def update_bill_status(order_id: str, status: str) -> None:
    bills = list_bills()
    changed = False
    for b in bills:
        if b["order_id"] == order_id:
            b["status"] = status
            changed = True
    if changed:
        write_lines("Bill.txt", [bill_to_line(b) for b in bills])
        _write_bill_details(bills)


def _write_bill_details(bills: List[Dict]) -> None:
    rows = []
    for bill in bills:
        for item in bill.get("items", []):
            discount = item.get("discount", float(item.get("unit_discount", 0)) * float(item["quantity"]))
            rows.append(
                f'{bill["bill_id"]} {item["acronym"]} {item["unit_price"]} '
                f'{item["quantity"]} {round(float(discount), 2)}'
            )
    write_lines("BillDetail.txt", rows)

def save_bill_record(bill: Dict) -> None:
    if len(str(bill.get("bill_id", ""))) != 9 or not str(bill["bill_id"]).isdigit():
        raise ValueError("Bill ID must be a unique 9-digit number.")
    if any(b["bill_id"] == str(bill["bill_id"]) for b in list_bills()):
        raise ValueError("Bill ID already exists.")
    append_line("Bill.txt", bill_to_line(bill))
    all_bills = list_bills()
    for saved in all_bills:
        if saved["bill_id"] == str(bill["bill_id"]):
            saved["items"] = bill.get("items", [])
            break
    _write_bill_details(all_bills)


def replace_bill_record(bill: Dict) -> None:
    bills = list_bills()
    found = False
    for index, saved in enumerate(bills):
        if saved["bill_id"] == str(bill["bill_id"]):
            bills[index] = bill
            found = True
            break
    if not found:
        raise ValueError("Bill not found.")
    write_lines("Bill.txt", [bill_to_line(row) for row in bills])
    _write_bill_details(bills)


def write_receipt_file(bill: Dict, order: Dict, payment_mode: str = "", merchant: str = "") -> Path:
    RECEIPTS_DIR.mkdir(parents=True, exist_ok=True)
    path = RECEIPTS_DIR / f"{bill['bill_id']}.txt"
    lines = [
        "=" * 42,
        f"       {SHOP_NAME}",
        "=" * 42,
        f"Bill ID   : {bill['bill_id']}",
        f"Order ID  : {bill['order_id']}",
        f"Date/Time : {bill.get('date') or order.get('date', '')} {bill.get('time') or order.get('time', '')}",
        f"Customer  : {bill['customer']}",
        "-" * 42,
        f"{'Item':<12}{'Qty':>6}{'Price':>10}{'Total':>12}",
        "-" * 42,
    ]
    for it in bill["items"]:
        lt = (it["unit_price"] - it["unit_discount"]) * it["quantity"]
        lines.append(f"{it['acronym']:<12}{it['quantity']:>6}{it['unit_price']:>10.2f}{lt:>12.2f}")
    lines += [
        "-" * 42,
        f"Subtotal (ex tax) : {bill['total_without_tax']:.2f} PKR",
        f"Tax               : {bill['total_tax']:.2f} PKR",
        f"Discount          : {bill['total_discount']:.2f} PKR",
        f"Seasonal discount : {bill.get('seasonal_discount', 0):.2f} PKR",
        f"Special discount  : {bill.get('special_discount', 0):.2f} PKR",
        f"PAYABLE           : {bill['payable']:.2f} PKR",
        f"Payment           : {payment_mode or order.get('payment_mode', '')} {merchant or order.get('merchant', '')}",
        f"Status            : {bill['status']}",
        "=" * 42,
        "        Thank you for shopping!",
        "=" * 42,
    ]
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return path


def build_cashier_items(raw_items: List[Dict]) -> List[Dict]:
    """raw_items: product_id, quantity — enrich from Product.txt"""
    from .products import validate_qty

    items = []
    for r in raw_items:
        p = get_by_id(str(r["product_id"]))
        if not p:
            raise ValueError(f"Unknown product {r['product_id']}")
        if float(p["qty"]) <= 0:
            raise ValueError(f"{p['name']} is out of stock")
        qty = float(r["quantity"])
        ok, msg = validate_qty(qty, p["qty"], p["unit"])
        if not ok:
            raise ValueError(msg)
        items.append({
            "product_id": p["id"],
            "barcode": p["barcode"],
            "acronym": p["acronym"],
            "name": p["name"],
            "unit": p["unit"],
            "unit_price": p["price"],
            "buy_price": p.get("buy_price", 0),
            "unit_discount": p["discount"],
            "tax": p["tax"],
            "quantity": qty,
            "available": p["qty"],
        })
    return items

"""Zain Mart — Barcode POS web application."""
from __future__ import annotations

import json
import os
import uuid
from datetime import datetime, timedelta
from functools import wraps
from pathlib import Path

from flask import (
    Flask,
    flash,
    g,
    jsonify,
    redirect,
    render_template,
    request,
    send_from_directory,
    session,
    url_for,
)

from modules import auth, barcode_utils, billing, categories, orders, portal, products, reports, seasons
from modules.file_io import (
    BARCODES_DIR,
    BASE_DIR,
    IMAGES_DIR,
    RECEIPTS_DIR,
    UPLOADS_DIR,
    backup_data,
    ensure_dirs,
)

app = Flask(__name__)
app.secret_key = os.environ.get("POS_SECRET", "zain-mart-pos-dev-key-change-me")
app.config["MAX_CONTENT_LENGTH"] = 16 * 1024 * 1024
# Same cookie across customer./sales./owner. subdomains when POS_BASE_DOMAIN is set
_base = os.environ.get("POS_BASE_DOMAIN", "").strip()
if _base:
    app.config["SESSION_COOKIE_DOMAIN"] = f".{_base.lstrip('.')}"


@app.before_request
def _bind_portal():
    portal.apply_portal()
    # Static files always allowed
    if request.endpoint and request.endpoint.startswith("static"):
        return None
    role = session.get("role")
    if role and role not in g.portal["allowed_roles"]:
        # Logged into wrong portal (e.g. customer cookie on owner subdomain)
        session.clear()
        flash("Please sign in on the correct portal for your role.", "error")
        return redirect(url_for("login"))
    return None


def init_app() -> None:
    ensure_dirs()
    auth.seed_users()
    categories.seed_categories()
    products.seed_products()
    billing.migrate_bills_file()
    # Generate barcodes + placeholder images for seed products
    for p in products.list_products():
        img = BARCODES_DIR / f"{p['barcode']}.png"
        if not img.exists():
            try:
                barcode_utils.generate_barcode_image(p["barcode"])
            except Exception:
                pass
        ph = IMAGES_DIR / f"{p['barcode']}.png"
        if not ph.exists():
            _make_placeholder(ph, p["name"], p["acronym"], p["category_name"])
    if not reports.list_expenses():
        # light sample expenses for charts
        today = datetime.now().strftime("%Y-%m-%d")
        reports.add_expense(today, "general", "Utilities", 3500)
    acc = orders.get_online_account()
    if not acc.get("bank"):
        orders.save_online_account("JazzCash", "Zain Mart", "03001234567", "03001234567")


def _make_placeholder(
    path: Path,
    name: str,
    acronym: str,
    category_name: str = "",
    force: bool = False,
) -> None:
    """Create a product illustration tailored to its name and category."""
    if path.exists() and not force:
        return
    from PIL import Image, ImageDraw

    keywords = f"{name} {category_name}".casefold()
    palette = [
        ((245, 249, 235), (216, 236, 208), (28, 120, 83)),
        ((250, 240, 218), (244, 211, 170), (211, 107, 39)),
        ((231, 244, 247), (192, 225, 232), (43, 119, 144)),
        ((250, 232, 222), (241, 200, 178), (190, 83, 58)),
        ((242, 239, 218), (219, 224, 172), (109, 127, 54)),
        ((236, 237, 247), (204, 213, 237), (79, 91, 150)),
    ]
    c1, c2, accent = palette[sum(ord(ch) for ch in acronym or name) % len(palette)]
    img = Image.new("RGB", (640, 480), c1)
    draw = ImageDraw.Draw(img)
    for y in range(480):
        blend = y / 480
        color = tuple(int(c1[i] * (1 - blend) + c2[i] * blend) for i in range(3))
        draw.line((0, y, 640, y), fill=color)

    draw.ellipse((178, 330, 462, 386), fill=(199, 211, 199))
    if "vegetable" in keywords or any(word in keywords for word in ("tomato", "carrot", "potato", "onion", "pepper")):
        draw.ellipse((205, 190, 345, 320), fill=(214, 70, 53), outline=(169, 54, 44), width=5)
        draw.polygon([(274, 205), (248, 177), (273, 188), (289, 163), (294, 192), (321, 177), (306, 210)], fill=(48, 132, 74))
        draw.polygon([(356, 170), (448, 215), (382, 319), (324, 239)], fill=(235, 143, 47), outline=(193, 103, 27))
        for x in (350, 370, 390):
            draw.line((x, 205, x - 20, 181), fill=(54, 133, 67), width=9)
        draw.ellipse((128, 232, 255, 342), fill=(96, 157, 73), outline=(63, 117, 50), width=4)
        draw.line((150, 320, 220, 252), fill=(211, 232, 172), width=5)
    elif "milk" in keywords or "dairy" in keywords:
        draw.polygon([(231, 151), (359, 151), (359, 327), (231, 327)], fill=(250, 252, 246), outline=(93, 133, 139), width=5)
        draw.polygon([(231, 151), (270, 117), (397, 117), (359, 151)], fill=(223, 239, 237), outline=(93, 133, 139), width=5)
        draw.polygon([(359, 151), (397, 117), (397, 292), (359, 327)], fill=(209, 229, 228), outline=(93, 133, 139), width=5)
        draw.rounded_rectangle((247, 211, 344, 285), radius=13, fill=(88, 161, 182))
        draw.ellipse((266, 228, 324, 267), fill=(255, 255, 255))
    elif "oil" in keywords:
        draw.rounded_rectangle((267, 164, 373, 329), radius=27, fill=(232, 170, 61), outline=(161, 111, 32), width=5)
        draw.rectangle((293, 131, 346, 170), fill=(232, 170, 61), outline=(161, 111, 32), width=4)
        draw.rectangle((298, 111, 341, 136), fill=(56, 91, 65))
        draw.rounded_rectangle((278, 221, 362, 283), radius=12, fill=(255, 244, 205))
        draw.ellipse((302, 232, 338, 268), fill=(235, 174, 48))
    elif "tea" in keywords:
        draw.ellipse((221, 187, 382, 310), fill=(249, 248, 232), outline=(110, 125, 109), width=6)
        draw.rectangle((231, 236, 372, 294), fill=(122, 71, 40))
        draw.ellipse((344, 215, 416, 282), outline=(110, 125, 109), width=10)
        draw.arc((259, 133, 325, 210), 190, 345, fill=(255, 255, 255), width=7)
        draw.arc((301, 122, 367, 205), 190, 345, fill=(255, 255, 255), width=7)
        draw.ellipse((152, 187, 210, 218), fill=(65, 139, 77))
        draw.line((157, 214, 203, 191), fill=(219, 235, 190), width=4)
    elif any(word in keywords for word in ("rice", "flour", "sugar", "grain", "wheat")):
        draw.polygon([(231, 151), (361, 151), (382, 304), (214, 304)], fill=(241, 220, 174), outline=(147, 112, 67), width=5)
        draw.line((231, 166, 361, 166), fill=(147, 112, 67), width=7)
        draw.rounded_rectangle((239, 210, 355, 269), radius=12, fill=(255, 248, 226))
        draw.ellipse((276, 219, 318, 260), fill=accent)
        for i in range(6):
            draw.ellipse((170 + i * 18, 320 - (i % 2) * 9, 185 + i * 18, 331 - (i % 2) * 9), fill=(245, 226, 182))
    elif any(word in keywords for word in ("snack", "chip", "biscuit", "junk food", "chocolate")):
        draw.polygon([(245, 148), (362, 148), (388, 311), (218, 311)], fill=(230, 99, 52), outline=(153, 63, 39), width=5)
        draw.line((245, 160, 362, 160), fill=(255, 224, 169), width=8)
        draw.line((222, 300, 385, 300), fill=(255, 224, 169), width=8)
        draw.ellipse((263, 205, 344, 278), fill=(255, 223, 130))
        draw.ellipse((282, 218, 324, 260), fill=(193, 96, 42))
    else:
        draw.rounded_rectangle((235, 147, 373, 319), radius=18, fill=(250, 248, 237), outline=(90, 111, 101), width=5)
        draw.rectangle((235, 183, 373, 215), fill=accent)
        draw.rounded_rectangle((258, 236, 350, 291), radius=10, fill=(229, 237, 221))
        draw.ellipse((286, 246, 322, 281), fill=accent)

    draw.rounded_rectangle((32, 34, 608, 102), radius=22, fill=(255, 255, 255))
    draw.text((54, 51), (category_name or "Product").upper()[:28], fill=accent)
    draw.rounded_rectangle((32, 370, 608, 452), radius=22, fill=(27, 49, 42))
    draw.text((54, 388), (name or "Product")[:36], fill=(255, 255, 255))
    draw.text((54, 421), "Illustration · Upload a photo for an exact match", fill=(220, 229, 202))
    path.parent.mkdir(parents=True, exist_ok=True)
    img.save(path, format="PNG")


def _read_product_picture(upload) -> bytes:
    from io import BytesIO

    from PIL import Image, UnidentifiedImageError

    try:
        with Image.open(upload.stream) as image:
            image.load()
            mode = "RGBA" if "A" in image.getbands() else "RGB"
            output = BytesIO()
            image.convert(mode).save(output, format="PNG")
            return output.getvalue()
    except (UnidentifiedImageError, OSError) as exc:
        raise ValueError("The uploaded product photo is not a valid image.") from exc


# ---------- auth helpers ----------

def login_required(roles=None):
    def decorator(fn):
        @wraps(fn)
        def wrapper(*args, **kwargs):
            wants_json = request.path.startswith("/api/") or request.accept_mimetypes.best == "application/json"

            def deny(message, code=401):
                if wants_json:
                    return jsonify({"ok": False, "message": message}), code
                flash(message, "error")
                return redirect(url_for("login" if "role" not in session else "home"))

            if "role" not in session:
                return deny("Please sign in again on this portal.")
            # Portal lock: role must match this subdomain
            if session.get("role") not in g.portal["allowed_roles"]:
                session.clear()
                return deny("Wrong portal for this account.")
            if roles and session.get("role") not in roles:
                return deny("You do not have access to that action.", 403)
            return fn(*args, **kwargs)

        return wrapper

    return decorator


@app.context_processor
def inject_globals():
    return {
        "shop_name": billing.SHOP_NAME,
        "user_role": "Manager" if session.get("role") == "Owner" else session.get("role"),
        "user_name": session.get("name"),
        "portal_key": getattr(g, "portal_key", "customer"),
        "portal_label": getattr(g, "portal_label", "Customer shop"),
        "portal_urls": {
            "customer": portal.portal_url("customer"),
            "sales": portal.portal_url("sales"),
            "owner": portal.portal_url("owner"),
        },
    }


@app.route("/bill/<bill_id>/print")
@login_required(roles=["Owner", "Salesperson"])
def bill_print(bill_id):
    bill = billing.get_bill_by_id(bill_id)
    if not bill:
        flash("Bill not found.", "error")
        return redirect(url_for("owner_orders") if session.get("role") == "Owner" else url_for("sales_order"))
    order = orders.get_order(bill["order_id"]) or {}
    return render_template("bill_print.html", bill=bill, order=order)


# ---------- static data folders ----------

@app.route("/barcodes/<path:filename>")
def serve_barcode(filename):
    return send_from_directory(BARCODES_DIR, filename)


@app.route("/images/<path:filename>")
def serve_image(filename):
    return send_from_directory(IMAGES_DIR, filename)


@app.route("/uploads/payments/<path:filename>")
@login_required(roles=["Owner"])
def serve_payment_shot(filename):
    return send_from_directory(UPLOADS_DIR, filename)


@app.route("/receipts/<path:filename>")
@login_required()
def serve_receipt(filename):
    return send_from_directory(RECEIPTS_DIR, filename)


# ---------- login ----------

@app.route("/")
def index():
    if session.get("role"):
        return redirect(url_for("home"))
    return redirect(url_for("login"))


@app.route("/login", methods=["GET", "POST"])
def login():
    """Login is portal-specific: customer. / sales. / owner. subdomains."""
    p = g.portal
    setup = {
        "owner_ready": auth.role_is_registered("Owner"),
        "sales_ready": auth.role_is_registered("Salesperson"),
        "need_setup": False,
    }
    if p["key"] == "owner":
        setup["need_setup"] = not setup["owner_ready"]
    elif p["key"] == "sales":
        setup["need_setup"] = not setup["sales_ready"]

    if request.method == "POST":
        action = request.form.get("action", "login")
        role = p["role"]  # forced by subdomain — cannot switch role here

        if p["key"] == "customer":
            email = request.form.get("email", "").strip()
            ok, msg = auth.validate_customer_email(email)
            if not ok:
                flash(msg, "error")
                return render_template("login.html", setup=setup)
            session.clear()
            session["role"] = "Customer"
            session["name"] = email
            session["email"] = email
            return redirect(url_for("customer_products"))

        if action == "recover":
            mobile = request.form.get("mobile", "").strip()
            ok, msg, new_pass = auth.recover_password_to_mobile(mobile, role)
            flash(msg, "success" if ok else "error")
            if ok:
                flash(f"Temporary password (sent to mobile): {new_pass}", "success")
            return redirect(url_for("login"))

        if action == "register":
            ok, msg = auth.register_role(
                request.form.get("username", ""),
                request.form.get("password", ""),
                role,
                request.form.get("mobile", ""),
            )
            flash(msg, "success" if ok else "error")
            return redirect(url_for("login"))

        username = request.form.get("username", "").strip()
        password = request.form.get("password", "")
        ok, msg = auth.validate_owner_sales(username, password, role)
        if not ok:
            flash(msg, "error")
            return render_template("login.html", setup=setup)
        session.clear()
        session["role"] = role
        session["name"] = username
        return redirect(url_for("home"))

    return render_template("login.html", setup=setup)


@app.route("/logout")
def logout():
    session.clear()
    return redirect(url_for("login"))


@app.route("/home")
@login_required()
def home():
    role = session["role"]
    if role == "Owner":
        return redirect(url_for("owner_dashboard"))
    if role == "Salesperson":
        return redirect(url_for("sales_order"))
    return redirect(url_for("customer_products"))


# ---------- barcode tools (owner + sales) ----------

@app.route("/barcode/generate", methods=["GET", "POST"])
@login_required(roles=["Owner"])
def barcode_generate():
    result = None
    if request.method == "POST":
        category_id = request.form.get("category_id", "").strip()
        number = request.form.get("number", "").strip()
        if not number:
            try:
                number = barcode_utils.new_category_barcode(category_id)
            except Exception as e:
                flash(str(e), "error")
                return render_template("barcode_generate.html", result=None,
                                       stock_list=products.list_products(), categories=categories.list_categories())
        ok, msg = barcode_utils.validate_category_barcode(number, category_id)
        if not ok:
            flash(msg, "error")
        elif products.barcode_exists(number):
            flash("Barcode already exists in Product.txt and cannot be reused.", "error")
        else:
            try:
                path = barcode_utils.generate_barcode_image(number)
                result = {"number": number, "url": url_for("serve_barcode", filename=path.name)}
                flash("Barcode generated and saved.", "success")
            except Exception as e:
                flash(str(e), "error")
    return render_template(
        "barcode_generate.html",
        result=result,
        stock_list=products.list_products(),
        categories=categories.list_categories(),
    )


@app.route("/barcode/reader", methods=["GET", "POST"])
@login_required(roles=["Owner", "Salesperson"])
def barcode_reader():
    product = None
    code = None
    if request.method == "POST":
        code = request.form.get("code", "").strip()
        if "image" in request.files and request.files["image"].filename:
            decoded = barcode_utils.decode_barcode_image(request.files["image"])
            if not decoded:
                flash("Barcode not recognised", "error")
            else:
                code = decoded
        if code:
            product = products.get_by_barcode(code)
            if not product:
                flash("Barcode not recognised", "error")
            elif product and float(product["qty"]) <= 0:
                flash("Out of stock", "error")
    return render_template(
        "barcode_reader.html",
        product=product,
        code=code,
        stock_list=products.list_products(),
    )


@app.route("/api/scan", methods=["POST"])
@login_required(roles=["Owner", "Salesperson"])
def api_scan():
    data = request.get_json(silent=True) or {}
    code = (data.get("code") or "").strip()
    if not code:
        return jsonify({"ok": False, "message": "Barcode not recognised"}), 400
    product = products.get_by_barcode(code)
    if not product:
        return jsonify({"ok": False, "message": "Barcode not recognised"}), 404
    return jsonify({"ok": True, "product": product})


# ---------- salesperson ----------

@app.route("/sales/order")
@login_required(roles=["Owner", "Salesperson"])
def sales_order():
    hold = orders.list_orders(status="on-hold") + orders.list_orders(status="in-progress")
    search_order = orders.get_order(request.args.get("order_id", "").strip()) if request.args.get("order_id") else None
    return render_template(
        "sales/order.html",
        products=products.list_products(),
        hold_orders=hold,
        search_order=search_order,
    )


@app.route("/owner/categories", methods=["GET", "POST"])
@login_required(roles=["Owner"])
def owner_categories():
    if request.method == "POST":
        try:
            categories.save_category(
                request.form.get("category_id", ""),
                request.form.get("name", ""),
                request.form.get("acronym", ""),
            )
            flash("Category saved.", "success")
        except Exception as e:
            flash(str(e), "error")
        return redirect(url_for("owner_categories"))
    return render_template("owner/categories.html", categories=categories.list_categories())


@app.route("/owner/seasons", methods=["GET", "POST"])
@login_required(roles=["Owner"])
def owner_seasons():
    if request.method == "POST":
        try:
            seasons.save_season(
                request.form.get("start", ""),
                request.form.get("end", ""),
                float(request.form.get("discount") or 0),
                request.form.get("active") == "true",
            )
            flash("Seasonal discount saved.", "success")
        except Exception as e:
            flash(str(e), "error")
        return redirect(url_for("owner_seasons"))
    return render_template("owner/seasons.html", seasons=seasons.list_seasons())


@app.route("/api/sales/calc", methods=["POST"])
@login_required(roles=["Owner", "Salesperson"])
def api_sales_calc():
    data = request.get_json(force=True)
    try:
        items = billing.build_cashier_items(data.get("items") or [])
        bill_discount = float(data.get("bill_discount") or 0)
        disc_type = data.get("discount_type") or "fixed"
        if disc_type == "percent" and bill_discount > 8:
            raise ValueError("Special discount cannot exceed 8%.")
        if disc_type == "percent":
            # convert percent of subtotal+tax before bill discount
            prelim = billing.calculate_bill(items, 0)
            bill_discount = prelim["payable"] * (bill_discount / 100.0)
        base_calc = billing.calculate_bill(items, 0)
        if bill_discount > base_calc["payable"] * 0.08:
            raise ValueError("Special discount cannot exceed 8% of the bill.")
        season_discount = base_calc["payable"] * seasons.active_discount() / 100
        calc = billing.calculate_bill(items, bill_discount, season_discount)
        return jsonify({"ok": True, "calc": calc})
    except Exception as e:
        return jsonify({"ok": False, "message": str(e)}), 400


@app.route("/api/sales/complete", methods=["POST"])
@login_required(roles=["Owner", "Salesperson"])
def api_sales_complete():
    data = request.get_json(force=True)
    try:
        items = billing.build_cashier_items(data.get("items") or [])
        bill_discount = float(data.get("bill_discount") or 0)
        if data.get("discount_type") == "percent" and bill_discount > 8:
            raise ValueError("Special discount cannot exceed 8%.")
        if data.get("discount_type") == "percent":
            prelim = billing.calculate_bill(items, 0)
            bill_discount = prelim["payable"] * (bill_discount / 100.0)
        base_calc = billing.calculate_bill(items, 0)
        if bill_discount > base_calc["payable"] * 0.08:
            raise ValueError("Special discount cannot exceed 8% of the bill.")
        season_discount = base_calc["payable"] * seasons.active_discount() / 100
        calc = billing.calculate_bill(items, bill_discount, season_discount)
        customer = (data.get("customer") or "Walk_In").strip()
        contact = (data.get("contact") or "-").strip() or "-"
        status = data.get("status") or "in-progress"
        payment_mode = data.get("payment_mode") or "cash"
        merchant = data.get("merchant") or "-"
        amount = float(data.get("amount_received") or 0)
        if status == "paid" and payment_mode == "cash" and amount < calc["payable"]:
            return jsonify({"ok": False, "message": "Amount received is less than payable."}), 400

        order = orders.create_order_with_details(
            customer=customer,
            contact=contact,
            items=[{"product_id": i["product_id"], "barcode": i["barcode"], "quantity": i["quantity"]} for i in calc["items"]],
            status=status,
            payment_amount=calc["payable"] if status == "paid" else 0,
            payment_mode=payment_mode if status == "paid" else "cash",
            merchant=merchant if status == "paid" else "-",
        )
        bill = {
            "bill_id": billing.make_bill_id(),
            "customer": customer,
            "order_id": order["order_id"],
            "items": calc["items"],
            "total_without_tax": calc["total_without_tax"],
            "total_tax": calc["total_tax"],
            "total_discount": calc["total_discount"],
            "payable": calc["payable"],
            "amount_received": amount if status == "paid" else 0,
            "balance_returned": round(max(0, amount - calc["payable"]), 2) if status == "paid" else 0,
            "status": status,
            "seasonal_discount": calc["seasonal_discount"],
            "special_discount": calc["special_discount"],
            "date": order["date"],
            "time": order["time"],
        }
        billing.save_bill_record(bill)
        receipt_path = billing.write_receipt_file(bill, order, payment_mode, merchant)
        change = round(amount - calc["payable"], 2) if payment_mode == "cash" and status == "paid" else 0
        return jsonify({
            "ok": True,
            "order": order,
            "bill": bill,
            "calc": calc,
            "change": change,
            "receipt_url": url_for("serve_receipt", filename=receipt_path.name),
            "bill_print_url": url_for("bill_print", bill_id=bill["bill_id"]),
        })
    except Exception as e:
        return jsonify({"ok": False, "message": str(e)}), 400


@app.route("/sales/resume/<order_id>")
@login_required(roles=["Owner", "Salesperson"])
def sales_resume(order_id):
    order = orders.get_order(order_id)
    if not order:
        flash("Order not found", "error")
        return redirect(url_for("sales_order"))
    details = orders.get_order_details(order_id)
    items = []
    for d in details:
        p = products.get_by_id(d["product_id"])
        if p:
            items.append({
                "product_id": p["id"],
                "barcode": p["barcode"],
                "acronym": p["acronym"],
                "name": p["name"],
                "unit": p["unit"],
                "unit_price": p["price"],
                "unit_discount": p["discount"],
                "tax": p["tax"],
                "quantity": d["quantity"],
                "available": p["qty"],
            })
    bill = billing.get_bill_by_order(order_id)
    return render_template(
        "sales/resume.html",
        order=order,
        items=items,
        bill=bill,
        products=products.list_products(),
        can_edit=order["status"] in ("on-hold", "in-progress"),
    )


@app.route("/api/sales/edit/<order_id>", methods=["POST"])
@login_required(roles=["Owner", "Salesperson"])
def api_sales_edit(order_id):
    data = request.get_json(force=True)
    order = orders.get_order(order_id)
    if not order:
        return jsonify({"ok": False, "message": "Order not found"}), 404
    if order["status"] not in ("paid", "on-hold", "in-progress"):
        return jsonify({"ok": False, "message": "This order cannot be edited."}), 400
    if order["status"] == "paid":
        try:
            paid_at = datetime.strptime(f'{order["date"]} {order["time"]}', "%Y-%m-%d %H:%M:%S")
        except ValueError:
            return jsonify({"ok": False, "message": "The order payment date is invalid."}), 400
        if paid_at > datetime.now() or datetime.now() - paid_at > timedelta(days=7):
            return jsonify({"ok": False, "message": "Paid orders can only be edited within 7 days."}), 400
        if session.get("role") != "Owner":
            manager_user = data.get("manager_username", "")
            manager_password = data.get("manager_password", "")
            approved, message = auth.validate_owner_sales(manager_user, manager_password, "Owner")
            if not approved:
                return jsonify({"ok": False, "message": f"Manager approval failed: {message}"}), 403
    try:
        raw_items = data.get("items") or []
        if not raw_items:
            raise ValueError("An order must contain at least one product.")
        old_qty = {}
        for detail in orders.get_order_details(order_id):
            old_qty[detail["product_id"]] = old_qty.get(detail["product_id"], 0) + detail["quantity"]
        items = []
        total_qty = {}
        for row in raw_items:
            pid = str(row.get("product_id", ""))
            p = products.get_by_id(pid)
            if not p:
                raise ValueError(f"Unknown product {pid}.")
            qty = float(row.get("quantity", 0))
            total_qty[pid] = total_qty.get(pid, 0) + qty
            ok, msg = products.validate_qty(total_qty[pid], float(p["qty"]) + old_qty.get(pid, 0), p["unit"])
            if not ok:
                raise ValueError(msg)
            items.append({
                "product_id": pid, "barcode": p["barcode"], "acronym": p["acronym"],
                "name": p["name"], "unit": p["unit"], "unit_price": p["price"],
                "buy_price": p["buy_price"], "unit_discount": p["discount"],
                "tax": p["tax"], "quantity": qty,
            })
        bill = billing.get_bill_by_order(order_id)
        if not bill:
            raise ValueError("Bill not found for this order.")
        calc = billing.calculate_bill(
            items,
            bill.get("special_discount", 0),
            bill.get("seasonal_discount", 0),
        )
        if order["status"] == "paid" and float(bill.get("amount_received", order["payment_amount"])) < calc["payable"]:
            raise ValueError("The edited total exceeds the amount already received.")
        orders.replace_order_details(order_id, [
            {"product_id": item["product_id"], "barcode": item["barcode"], "quantity": item["quantity"]}
            for item in calc["items"]
        ])
        bill.update({
            "items": calc["items"], "total_without_tax": calc["total_without_tax"],
            "total_tax": calc["total_tax"], "total_discount": calc["unit_discount_total"],
            "payable": calc["payable"],
            "seasonal_discount": calc["seasonal_discount"],
            "special_discount": calc["special_discount"],
            "balance_returned": max(0, float(bill.get("amount_received", 0)) - calc["payable"]),
        })
        billing.replace_bill_record(bill)
        updated = orders.update_order_status(
            order_id, order["status"],
            payment_amount=calc["payable"] if order["status"] == "paid" else order["payment_amount"],
        )
        return jsonify({"ok": True, "order": updated, "bill": bill})
    except Exception as e:
        return jsonify({"ok": False, "message": str(e)}), 400


@app.route("/api/sales/pay/<order_id>", methods=["POST"])
@login_required(roles=["Owner", "Salesperson"])
def api_sales_pay(order_id):
    data = request.get_json(force=True)
    order = orders.get_order(order_id)
    if not order:
        return jsonify({"ok": False, "message": "Order not found"}), 404
    bill = billing.get_bill_by_order(order_id)
    payable = bill["payable"] if bill else order["payment_amount"]
    mode = data.get("payment_mode") or "cash"
    merchant = data.get("merchant") or "-"
    amount = float(data.get("amount_received") or payable)
    status = data.get("status") or "paid"
    if status == "paid" and mode == "cash" and amount < payable:
        return jsonify({"ok": False, "message": "Invalid payment amount"}), 400
    updated = orders.update_order_status(
        order_id,
        status,
        payment_amount=payable if status == "paid" else order["payment_amount"],
        payment_mode=mode,
        merchant=merchant,
    )
    if bill:
        bill["status"] = status
        bill["amount_received"] = amount if status == "paid" else bill.get("amount_received", 0)
        bill["balance_returned"] = round(max(0, amount - payable), 2) if status == "paid" else 0
        billing.replace_bill_record(bill)
        billing.write_receipt_file(bill, updated or order, mode, merchant)
    change = round(amount - payable, 2) if mode == "cash" and status == "paid" else 0
    return jsonify({"ok": True, "order": updated, "change": change, "payable": payable})


# ---------- owner ----------

@app.route("/owner")
@login_required(roles=["Owner"])
def owner_dashboard():
    month = datetime.now().strftime("%Y-%m")
    sales = reports.monthly_sales(month)
    profit = reports.daily_profit(14)
    pending = orders.list_online_payments(status="pending")
    low = products.low_stock()
    return render_template(
        "owner/dashboard.html",
        sales=sales,
        profit=profit,
        pending_count=len(pending),
        low_stock=low,
    )


@app.route("/owner/stock", methods=["GET", "POST"])
@login_required(roles=["Owner"])
def owner_stock():
    if request.method == "POST":
        action = request.form.get("action")
        try:
            if action == "delete":
                products.delete_product(request.form.get("id"))
                flash("Product deleted.", "success")
            else:
                barcode = request.form.get("barcode", "").strip()
                ok, msg = barcode_utils.validate_six_digit(barcode)
                if not ok:
                    raise ValueError(msg)
                sell = float(request.form.get("sell_price") or request.form.get("price") or 0)
                buy = float(request.form.get("buy_price") or 0)
                data = {
                    "id": request.form.get("id") or products.next_product_id(),
                    "category_id": request.form.get("category_id", ""),
                    "barcode": barcode,
                    "acronym": (request.form.get("acronym") or "")[:8].upper(),
                    "name": request.form.get("name", "").strip(),
                    "unit": request.form.get("unit", "count"),
                    "price": sell,
                    "sell_price": sell,
                    "buy_price": buy,
                    "tax": float(request.form.get("tax") or 0),
                    "discount": float(request.form.get("discount") or 0),
                    "qty": float(request.form.get("qty") or 0),
                    "barcode_path": f"barcodes/{barcode}.png",
                }
                pic = request.files.get("picture")
                picture_data = _read_product_picture(pic) if pic and pic.filename else None
                if action == "edit":
                    products.update_product(data["id"], data)
                    flash("Product updated.", "success")
                else:
                    products.add_product(data)
                    flash("Product added.", "success")
                try:
                    barcode_utils.generate_barcode_image(barcode)
                except Exception:
                    pass
                ph = IMAGES_DIR / f"{barcode}.png"
                if picture_data is not None:
                    ph.write_bytes(picture_data)
                else:
                    category = categories.get_category(data["category_id"])
                    _make_placeholder(
                        ph,
                        data["name"],
                        data["acronym"],
                        category["name"] if category else "",
                        force=not ph.exists(),
                    )
        except Exception as e:
            flash(str(e), "error")
        return redirect(url_for("owner_stock"))
    return render_template(
        "owner/stock.html",
        products=products.list_products(),
        categories=categories.list_categories(),
        raw=reports.list_raw_materials(),
        low=products.low_stock(),
        next_id=products.next_product_id(),
        requests=products.list_requests("pending"),
    )


@app.route("/owner/expenses", methods=["GET", "POST"])
@login_required(roles=["Owner"])
def owner_expenses():
    if request.method == "POST":
        try:
            reports.add_expense(
                request.form.get("date") or datetime.now().strftime("%Y-%m-%d"),
                request.form.get("category") or "general",
                request.form.get("description") or "Expense",
                float(request.form.get("amount") or 0),
            )
            flash("Expense saved.", "success")
        except Exception as e:
            flash(str(e), "error")
        return redirect(url_for("owner_expenses"))
    return render_template("owner/expenses.html", expenses=reports.list_expenses())


@app.route("/owner/reports")
@login_required(roles=["Owner"])
def owner_reports():
    month = request.args.get("month") or datetime.now().strftime("%Y-%m")
    mode = request.args.get("payment_mode") or ""
    y, m = map(int, month.split("-"))
    prev = f"{y - 1}-12" if m == 1 else f"{y}-{m - 1:02d}"
    sales = reports.monthly_sales(month)
    if mode:
        # filter orders for display note
        filtered = reports.paid_orders_in_range(f"{month}-01", f"{month}-31", mode)
        sales["filter_mode"] = mode
        sales["filter_count"] = len(filtered)
        sales["filter_total"] = round(sum(o["payment_amount"] for o in filtered), 2)
    profit = reports.daily_profit(31)
    best = reports.best_sellers(month)
    growth = reports.growth_compare(month, prev)
    final = reports.final_report(month, prev)
    start = request.args.get("start") or f"{month}-01"
    end = request.args.get("end") or datetime.now().strftime("%Y-%m-%d")
    return render_template(
        "owner/reports.html",
        month=month,
        prev=prev,
        sales=sales,
        profit=profit,
        best=best,
        growth=growth,
        final=final,
        payment_mode=mode,
        incomplete=reports.order_status_report(("in-progress", "on-hold")),
        completed=reports.order_status_report(("paid", "archived")),
        cash_sales=reports.cash_sales_report(start, end),
        sales_summary=reports.all_sales_report(),
        top_sold=reports.top_sold_with_stock(),
        top_remaining=reports.top_remaining(),
        start=start,
        end=end,
    )


@app.route("/owner/reports/download")
@login_required(roles=["Owner"])
def owner_report_download():
    month = request.args.get("month") or datetime.now().strftime("%Y-%m")
    final = reports.final_report(month)
    text = reports.report_as_text(final)
    path = RECEIPTS_DIR / f"report_{month}.txt"
    path.write_text(text, encoding="utf-8")
    return send_from_directory(RECEIPTS_DIR, path.name, as_attachment=True)


@app.route("/owner/account", methods=["GET", "POST"])
@login_required(roles=["Owner"])
def owner_account():
    if request.method == "POST":
        orders.save_online_account(
            request.form.get("bank", ""),
            request.form.get("title", ""),
            request.form.get("number", ""),
            request.form.get("phone", ""),
        )
        flash("Online account details saved.", "success")
        return redirect(url_for("owner_account"))
    return render_template("owner/account.html", account=orders.get_online_account())


@app.route("/owner/payments", methods=["GET", "POST"])
@login_required(roles=["Owner"])
def owner_payments():
    if request.method == "POST":
        order_id = request.form.get("order_id")
        decision = request.form.get("decision")
        if decision == "verified":
            orders.set_online_payment_status(order_id, "verified")
            orders.update_order_status(order_id, "paid", payment_mode="online")
            flash(f"Order {order_id} verified and marked paid.", "success")
        else:
            orders.set_online_payment_status(order_id, "rejected")
            orders.update_order_status(order_id, "on-hold")
            flash(f"Order {order_id} payment rejected.", "success")
        return redirect(url_for("owner_payments"))
    return render_template(
        "owner/payments.html",
        payments=orders.list_online_payments(),
    )


@app.route("/owner/orders")
@login_required(roles=["Owner"])
def owner_orders():
    return render_template(
        "owner/orders.html",
        orders=orders.list_orders(),
        bills=billing.list_bills(),
    )


@app.route("/owner/credentials", methods=["GET", "POST"])
@login_required(roles=["Owner"])
def owner_credentials():
    if request.method == "POST":
        action = request.form.get("action")
        try:
            if action == "mobile":
                ok, msg = auth.update_owner_mobile(request.form.get("mobile", ""))
                flash(msg, "success" if ok else "error")
            elif action == "reset":
                ok, msg = auth.owner_reset_role(
                    request.form.get("role"),
                    request.form.get("username"),
                    request.form.get("password"),
                )
                flash(msg, "success" if ok else "error")
            elif action == "unlock":
                auth.unlock_role_for_reset(request.form.get("role"))
                flash(f"{request.form.get('role')} unlocked — register a new ID on the login page.", "success")
        except Exception as e:
            flash(str(e), "error")
        return redirect(url_for("owner_credentials"))
    return render_template(
        "owner/credentials.html",
        users=auth.list_users(),
        owner_mobile=auth.get_owner_mobile(),
    )


@app.route("/owner/backup", methods=["POST"])
@login_required(roles=["Owner"])
def owner_backup():
    path = backup_data()
    flash(f"Backup created at {path}", "success")
    return redirect(url_for("owner_dashboard"))


# ---------- customer ----------

@app.route("/shop")
@login_required(roles=["Customer"])
def customer_products():
    return render_template("customer/products.html", products=products.list_products())


@app.route("/shop/cart", methods=["GET", "POST"])
@login_required(roles=["Customer"])
def customer_cart():
    email = session["email"]
    if request.method == "POST":
        action = request.form.get("action")
        try:
            if action == "add":
                orders.add_to_cart(email, request.form.get("product_id"), float(request.form.get("quantity") or 1))
                flash("Added to cart.", "success")
            elif action == "update":
                pid = request.form.get("product_id")
                qty = float(request.form.get("quantity") or 1)
                cart = orders.get_cart(email)
                for it in cart:
                    if it["product_id"] == pid:
                        p = products.get_by_id(pid)
                        if not p or float(p["qty"]) <= 0:
                            raise ValueError("Out of stock")
                        ok, msg = products.validate_qty(qty, p["qty"], p["unit"])
                        if not ok:
                            raise ValueError(msg)
                        it["quantity"] = qty
                orders.save_cart(
                    email,
                    [{"product_id": c["product_id"], "barcode": c["barcode"], "quantity": c["quantity"]} for c in cart],
                )
            elif action == "remove":
                pid = request.form.get("product_id")
                cart = [c for c in orders.get_cart(email) if c["product_id"] != pid]
                orders.save_cart(
                    email,
                    [{"product_id": c["product_id"], "barcode": c["barcode"], "quantity": c["quantity"]} for c in cart],
                )
                flash("Item removed.", "success")
        except Exception as e:
            flash(str(e), "error")
        if action == "add":
            return redirect(url_for("customer_products"))
        return redirect(url_for("customer_cart"))
    cart = orders.get_cart(email)
    total = round(sum(c["line_total"] for c in cart), 2)
    return render_template("customer/cart.html", cart=cart, total=total)


@app.route("/shop/checkout", methods=["GET", "POST"])
@login_required(roles=["Customer"])
def customer_checkout():
    email = session["email"]
    cart = orders.get_cart(email)
    if not cart:
        flash("Your cart is empty.", "error")
        return redirect(url_for("customer_products"))
    account = orders.get_online_account()
    if request.method == "POST":
        contact = (request.form.get("contact") or "-").strip() or "-"
        mode = request.form.get("payment_mode") or "cash"
        try:
            raw = [{"product_id": c["product_id"], "quantity": c["quantity"]} for c in cart]
            items = billing.build_cashier_items(raw)
            calc = billing.calculate_bill(items, 0)
            season_discount = calc["payable"] * seasons.active_discount() / 100
            calc = billing.calculate_bill(items, seasonal_discount=season_discount)
            fname = None
            txn = "-"
            if mode == "cash":
                status = "in-progress"
                merchant = "-"
                pay_mode = "cash"
            elif mode == "cod":
                status = "in-progress"
                merchant = "Cash_On_Delivery"
                pay_mode = "cash_on_delivery"
            else:
                status = "on-hold"
                merchant = account.get("bank") or "Online"
                pay_mode = "online"
                txn = (request.form.get("transaction_id") or "").strip()
                if not txn:
                    raise ValueError("Transaction id is required for online payment.")
                shot = request.files.get("screenshot")
                if not shot or not shot.filename:
                    raise ValueError("Upload a payment screenshot.")
                ext = Path(shot.filename).suffix.lower() or ".png"
                fname = f"{uuid.uuid4().hex}{ext}"
                UPLOADS_DIR.mkdir(parents=True, exist_ok=True)
                shot.save(UPLOADS_DIR / fname)
            order = orders.create_order_with_details(
                customer=email,
                contact=contact,
                items=[{"product_id": i["product_id"], "barcode": i["barcode"], "quantity": i["quantity"]} for i in calc["items"]],
                status=status,
                payment_amount=calc["payable"],
                payment_mode=pay_mode,
                merchant=merchant,
            )
            bill = {
                "bill_id": billing.make_bill_id(),
                "customer": email,
                "order_id": order["order_id"],
                "items": calc["items"],
                "total_without_tax": calc["total_without_tax"],
                "total_tax": calc["total_tax"],
                "total_discount": calc["total_discount"],
                "payable": calc["payable"],
                "amount_received": 0,
                "balance_returned": 0,
                "status": status,
                "seasonal_discount": calc["seasonal_discount"],
                "special_discount": 0,
                "date": order["date"],
                "time": order["time"],
            }
            billing.save_bill_record(bill)
            billing.write_receipt_file(bill, order, order["payment_mode"], merchant)
            if mode == "online" and fname:
                orders.add_online_payment(order["order_id"], email, txn, f"uploads/payments/{fname}", "pending")
            orders.clear_cart(email)
            if mode == "cash":
                flash(f"Order placed. Show order number {order['order_id']} at the counter.", "success")
            elif mode == "cod":
                flash(f"Cash on delivery order {order['order_id']} placed. Pay when you receive it.", "success")
            else:
                flash("Payment submitted. Status: Verification pending.", "success")
            return redirect(url_for("customer_history"))
        except Exception as e:
            flash(str(e), "error")
    total = round(sum(c["line_total"] for c in cart), 2)
    return render_template("customer/checkout.html", cart=cart, total=total, account=account)


@app.route("/shop/suggest", methods=["GET", "POST"])
@login_required(roles=["Customer"])
def customer_suggest():
    if request.method == "POST":
        try:
            name = (request.form.get("name") or "").strip()
            if not name:
                raise ValueError("Product name is required.")
            note = (request.form.get("note") or "-").strip() or "-"
            img_path = "-"
            pic = request.files.get("picture")
            if pic and pic.filename:
                req_dir = BASE_DIR / "uploads" / "requests"
                req_dir.mkdir(parents=True, exist_ok=True)
                ext = Path(pic.filename).suffix.lower() or ".png"
                fname = f"{uuid.uuid4().hex}{ext}"
                pic.save(req_dir / fname)
                img_path = f"uploads/requests/{fname}"
            products.add_request(session["email"], name, note, img_path)
            flash("Product suggestion sent to the shop. Picture is optional.", "success")
            return redirect(url_for("customer_products"))
        except Exception as e:
            flash(str(e), "error")
    return render_template("customer/suggest.html")


@app.route("/uploads/requests/<path:filename>")
@login_required(roles=["Owner", "Salesperson"])
def serve_request_image(filename):
    return send_from_directory(BASE_DIR / "uploads" / "requests", filename)


@app.route("/owner/requests", methods=["POST"])
@login_required(roles=["Owner"])
def owner_handle_request():
    rid = request.form.get("id")
    decision = request.form.get("decision")
    if decision == "approved":
        products.set_request_status(rid, "approved")
        flash("Request approved — add the product in Stock with buy & sell prices.", "success")
    else:
        products.set_request_status(rid, "rejected")
        flash("Request rejected.", "success")
    return redirect(url_for("owner_stock"))


@app.route("/sales/product", methods=["GET", "POST"])
@login_required(roles=["Owner"])
def sales_add_product():
    """Salesperson or owner can add a product with buy + sell price."""
    if request.method == "POST":
        try:
            barcode = request.form.get("barcode", "").strip()
            ok, msg = barcode_utils.validate_six_digit(barcode)
            if not ok:
                raise ValueError(msg)
            sell = float(request.form.get("sell_price") or 0)
            buy = float(request.form.get("buy_price") or 0)
            data = {
                "id": products.next_product_id(),
                "category_id": request.form.get("category_id", ""),
                "barcode": barcode,
                "acronym": (request.form.get("acronym") or "")[:8].upper(),
                "name": request.form.get("name", "").strip(),
                "unit": request.form.get("unit", "count"),
                "price": sell,
                "sell_price": sell,
                "buy_price": buy,
                "tax": float(request.form.get("tax") or 0),
                "discount": float(request.form.get("discount") or 0),
                "qty": float(request.form.get("qty") or 0),
                "barcode_path": f"barcodes/{barcode}.png",
            }
            pic = request.files.get("picture")
            picture_data = _read_product_picture(pic) if pic and pic.filename else None
            products.add_product(data)
            try:
                barcode_utils.generate_barcode_image(barcode)
            except Exception:
                pass
            ph = IMAGES_DIR / f"{barcode}.png"
            if picture_data is not None:
                ph.write_bytes(picture_data)
            else:
                category = categories.get_category(data["category_id"])
                _make_placeholder(
                    ph,
                    data["name"],
                    data["acronym"],
                    category["name"] if category else "",
                    force=True,
                )
            flash("Product saved with buy & sell prices.", "success")
            return redirect(url_for("sales_order") if session["role"] == "Salesperson" else url_for("owner_stock"))
        except Exception as e:
            flash(str(e), "error")
    return render_template("sales/add_product.html", next_id=products.next_product_id(), categories=categories.list_categories())


@app.route("/shop/history")
@login_required(roles=["Customer"])
def customer_history():
    email = session["email"]
    rows = orders.customer_orders(email)
    # attach payment verification status
    pays = {p["order_id"]: p for p in orders.list_online_payments()}
    for o in rows:
        pay = pays.get(o["order_id"])
        if o["payment_mode"] == "online" and pay and pay["status"] == "pending":
            o["display_status"] = "Verification pending"
        else:
            o["display_status"] = o["status"]
        o["bill"] = billing.get_bill_by_order(o["order_id"])
    return render_template("customer/history.html", orders=rows)


# ---------- boot ----------

init_app()

if __name__ == "__main__":
    # 127.0.0.1 is enough for *.localhost portals in modern browsers
    app.run(debug=True, host="127.0.0.1", port=5000)

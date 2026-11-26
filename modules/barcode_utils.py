"""Barcode generation (Code 128) and image decode helpers."""
from __future__ import annotations

from io import BytesIO
from pathlib import Path
import random
from typing import Optional, Tuple

from .file_io import BARCODES_DIR, BASE_DIR
from .products import get_by_barcode
from .categories import get_category


def validate_six_digit(code: str) -> Tuple[bool, str]:
    code = (code or "").strip()
    if not code.isdigit() or len(code) != 6:
        return False, "Barcode must be exactly 6 digits."
    if code[0] == "0":
        return False, "Barcode must start with a non-zero digit (100000–999999)."
    n = int(code)
    if n < 100000 or n > 999999:
        return False, "Barcode must be between 100000 and 999999."
    return True, ""


def validate_category_barcode(code: str, category_id: str) -> Tuple[bool, str]:
    ok, message = validate_six_digit(code)
    if not ok:
        return ok, message
    if not get_category(category_id):
        return False, "Configure the product category before generating a barcode."
    if not code.startswith(category_id):
        return False, "The first two barcode digits must match the selected category ID."
    return True, ""


def new_category_barcode(category_id: str) -> str:
    if not get_category(category_id):
        raise ValueError("Configure the product category before generating a barcode.")
    for _ in range(10000):
        code = f"{category_id}{random.randrange(10000):04d}"
        if not get_by_barcode(code) and not (BARCODES_DIR / f"{code}.png").exists():
            return code
    raise RuntimeError("Could not find an unused barcode for this category.")


def generate_barcode_image(number: str) -> Path:
    ok, msg = validate_six_digit(number)
    if not ok:
        raise ValueError(msg)
    try:
        import barcode
        from barcode.writer import ImageWriter
    except ImportError as e:
        raise RuntimeError("Install python-barcode and Pillow") from e

    BARCODES_DIR.mkdir(parents=True, exist_ok=True)
    out = BARCODES_DIR / f"{number}.png"
    code128 = barcode.get_barcode_class("code128")
    instance = code128(number, writer=ImageWriter())
    # python-barcode adds .png itself if we pass path without extension carefully
    instance.save(str(BARCODES_DIR / number), options={"write_text": True, "module_height": 12})
    if not out.exists():
        # some versions write without forcing path
        alt = BARCODES_DIR / f"{number}.png"
        if not alt.exists():
            raise RuntimeError("Barcode image was not created")
    return out


def decode_barcode_image(file_storage_or_path) -> Optional[str]:
    """Decode from UploadFile-like object or path. Returns 6-digit string or None."""
    try:
        from pyzbar.pyzbar import decode
        from PIL import Image
        import numpy as np
    except ImportError:
        # Fallback: try opencv only path if available
        return _decode_opencv(file_storage_or_path)

    if hasattr(file_storage_or_path, "read"):
        data = file_storage_or_path.read()
        if hasattr(file_storage_or_path, "seek"):
            file_storage_or_path.seek(0)
        img = Image.open(BytesIO(data))
    else:
        img = Image.open(str(file_storage_or_path))

    results = decode(img)
    for r in results:
        text = r.data.decode("utf-8", errors="ignore").strip()
        if text.isdigit() and len(text) == 6:
            return text
        # Code128 may encode as-is
        digits = "".join(c for c in text if c.isdigit())
        if len(digits) == 6:
            return digits
        if text.isdigit():
            return text
    return None


def _decode_opencv(file_storage_or_path) -> Optional[str]:
    try:
        import cv2
        from pyzbar.pyzbar import decode
    except ImportError:
        return None
    if hasattr(file_storage_or_path, "read"):
        import numpy as np
        data = np.frombuffer(file_storage_or_path.read(), dtype=np.uint8)
        if hasattr(file_storage_or_path, "seek"):
            file_storage_or_path.seek(0)
        img = cv2.imdecode(data, cv2.IMREAD_COLOR)
    else:
        img = cv2.imread(str(file_storage_or_path))
    if img is None:
        return None
    for r in decode(img):
        text = r.data.decode("utf-8", errors="ignore").strip()
        digits = "".join(c for c in text if c.isdigit())
        if len(digits) >= 6:
            return digits[:6] if len(digits) > 6 and digits[0] == "0" else (digits if len(digits) == 6 else text)
        if text:
            return text
    return None


def lookup_after_scan(code: str):
    product = get_by_barcode(code)
    return product

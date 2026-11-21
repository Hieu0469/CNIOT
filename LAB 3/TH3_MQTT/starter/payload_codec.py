"""Mã hoá / giải mã payload đặc trưng máy bơm (Lab TH3–TH4, CE339).  MÃ KHUNG – SV hoàn thành các TODO.

Bản tin = nhãn thời gian ts (uint32, giây Unix) + 12 đặc trưng float32:
  f[0..2]  = rms  x,y,z   (g)
  f[3..5]  = pk   x,y,z   (g, giá trị đỉnh |a|)
  f[6..8]  = kurt x,y,z   (kurtosis, không thứ nguyên)
  f[9..11] = eb   x,y,z   (năng lượng dải quanh 25 Hz, g^2)
Hai định dạng (phải KHỚP với firmware ESP32):
  - struct C packed, little-endian: struct { uint32_t ts; float f[12]; }  -> 52 byte
  - JSON gọn, mỗi số in "%.{PREC}f", giá trị đã kẹp trong [0; 9,99]:
    {"ts":1790000000,"rms":[a,b,c],"pk":[a,b,c],"kurt":[a,b,c],"eb":[a,b,c]}
"""
import json
import struct

FEAT_KEYS = ["rms", "pk", "kurt", "eb"]
STRUCT_FMT = "<I12f"                       # '<' = little-endian, không đệm (packed)
STRUCT_SIZE = struct.calcsize(STRUCT_FMT)  # 52
VMAX = 9.99                                # firmware kẹp mọi đặc trưng vào [0; 9,99]


def clamp(v: float) -> float:
    return min(max(float(v), 0.0), VMAX)


def encode_struct(ts: int, feats) -> bytes:
    """Đóng gói giống hệt struct C trên ESP32 (little-endian, packed)."""
    if len(feats) != 12:
        raise ValueError("cần đúng 12 đặc trưng")
    # TODO 6: dùng struct.pack với STRUCT_FMT
    return struct.pack(STRUCT_FMT, ts, *feats)


def decode_struct(b: bytes):
    """Trả về (ts, list 12 float). Ném ValueError nếu độ dài sai."""
    if len(b) != STRUCT_SIZE:
        raise ValueError(f"payload nhị phân phải {STRUCT_SIZE} B, nhận {len(b)} B")
    # TODO 7: struct.unpack rồi tách ts và 12 đặc trưng
    unpacked = struct.unpack(STRUCT_FMT, b)
    return unpacked[0], list(unpacked[1:])


def encode_json(ts: int, feats, prec: int) -> str:
    """JSON gọn, giống firmware: không khoảng trắng, mỗi số in với prec chữ số thập phân."""
    parts = [f'{{"ts":{ts}']
    for i, k in enumerate(FEAT_KEYS):
        vals = ",".join(f"{clamp(x):.{prec}f}" for x in feats[3 * i:3 * i + 3])
        parts.append(f'"{k}":[{vals}]')
    return ",".join(parts) + "}"


def json_len(prec: int, ts_digits: int = 10) -> int:
    """Độ dài JSON (ký tự = byte vì toàn ASCII) tính theo CÔNG THỨC của bạn (không gọi encode_json).
    Gợi ý: đếm số ký tự "khung" cố định ({, }, tên khoá, dấu ngoặc kép, :, [, ], dấu phẩy) + số chữ số của ts
    + 12 số, mỗi số có dạng "x.xxx" (1 chữ số nguyên vì giá trị < 10)."""
    # TODO 8: trả về công thức theo prec và ts_digits (phải khớp phiếu dự đoán của bạn)
    return 74 + ts_digits + 12 * prec


def decode_any(payload: bytes) -> dict:
    """Nhận diện định dạng: bắt đầu bằng '{' -> JSON, dài 52 B -> struct. Trả về dict {ts, rms:[..], ...}."""
    if payload[:1] == b"{":
        o = json.loads(payload.decode("utf-8"))
        o["fmt"] = "json"
        return o
    ts, f = decode_struct(payload)
    o = {"ts": ts, "fmt": "struct"}
    for i, k in enumerate(FEAT_KEYS):
        o[k] = f[3 * i:3 * i + 3]
    return o


def flat(o: dict):
    """dict (từ decode_any) -> list 12 đặc trưng theo thứ tự chuẩn."""
    return [float(x) for k in FEAT_KEYS for x in o[k]]


if __name__ == "__main__":
    # Bộ đặc trưng mẫu của bài giảng Ch.7
    TS = 1790000000
    F = [0.0123, 0.0131, 0.0207, 0.0412, 0.0455, 0.0689, 3.12, 2.98, 3.41, 0.00081, 0.00077, 0.00135]
    b = encode_struct(TS, F)
    assert len(b) == 52
    ts2, f2 = decode_struct(b)
    assert ts2 == TS and max(abs(x - y) for x, y in zip(F, f2)) < 1e-6
    for p in range(2, 6):
        js = encode_json(TS, F, p)
        assert len(js) == json_len(p), (p, len(js))
        err = max(abs(x - y) for x, y in zip(F, flat(decode_any(js.encode()))))
        print(f"PREC={p}: JSON {len(js):3d} B, sai số làm tròn lớn nhất {err:.2e}  |  {js[:60]}...")
    print(f"struct: {len(b)} B  hex: {b[:16].hex(' ')} ...")
    assert decode_any(b)["fmt"] == "struct"
    print("Tự kiểm tra payload_codec: ĐẠT")

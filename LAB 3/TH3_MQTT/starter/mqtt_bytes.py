"""Đếm byte gói MQTT 3.1.1 (Lab TH3 – CE339).  MÃ KHUNG – SV hoàn thành các TODO.

Công thức (Ch.7, mục "Cấu trúc gói MQTT" và "Ví dụ số: đếm byte một bản tin theo QoS"):
  PUBLISH = 1 (byte loại+cờ) + RL_bytes + 2 (độ dài topic) + len(topic) + [2 packet id nếu QoS>0] + payload
  PUBACK / PUBREC / PUBREL / PUBCOMP = 4 byte mỗi gói
Chạy:  python3 mqtt_bytes.py   -> tự kiểm tra bằng các số trong bài giảng, rồi in bảng cho cấu hình của bạn.
mqtt_proxy.py dùng hàm decode_rl() của file này để tách luồng byte thành từng gói.
"""

PACKET_NAMES = {1: "CONNECT", 2: "CONNACK", 3: "PUBLISH", 4: "PUBACK", 5: "PUBREC", 6: "PUBREL",
                7: "PUBCOMP", 8: "SUBSCRIBE", 9: "SUBACK", 10: "UNSUBSCRIBE", 11: "UNSUBACK",
                12: "PINGREQ", 13: "PINGRESP", 14: "DISCONNECT", 15: "AUTH"}


def rl_nbytes(n: int) -> int:
    """Số byte cần để mã hoá Remaining Length = n (1..4). Ví dụ: 127 -> 1, 128 -> 2, 16384 -> 3."""
    if n < 0 or n > 268_435_455:
        raise ValueError("Remaining Length ngoài phạm vi")
    # TODO 1: mỗi byte RL mang 7 bit giá trị
    if n < 128:
        return 1
    elif n < 16384:
        return 2
    elif n < 2097152:
        return 3
    else:
        return 4

def encode_rl(n: int) -> bytes:
    """Mã hoá Remaining Length: mỗi byte 7 bit giá trị (thấp trước), bit 7 = 1 nếu còn byte sau.
    Ví dụ: 163 -> b'\\xa3\\x01'."""
    # TODO 2
    out = bytearray()
    while True:
        encoded_byte = n % 128
        n //= 128
        if n > 0:
            encoded_byte |= 128
        out.append(encoded_byte)
        if n <= 0:
            break
    return bytes(out)


def decode_rl(buf, start: int = 1):
    """Giải mã Remaining Length bắt đầu tại buf[start] (buf[0] là byte loại+cờ).
    Trả về (giá trị, số byte RL); trả về None nếu buf CHƯA ĐỦ byte (proxy sẽ chờ thêm dữ liệu);
    ném ValueError nếu RL dài hơn 4 byte (gói lỗi)."""
    multiplier = 1
    value = 0
    index = start
    
    while True:
        if index >= len(buf):
            return None  # Không đủ dữ liệu
            
        encoded_byte = buf[index]
        value += (encoded_byte & 127) * multiplier
        multiplier *= 128
        index += 1
        
        # Nếu đã đọc 4 byte mà bit 7 (tiếp tục) vẫn bằng 1, đây là gói lỗi.
        if multiplier > 128 * 128 * 128:
            if (encoded_byte & 128) != 0:
                raise ValueError("Remaining Length dài hơn 4 byte (Malformed Packet)")
                
        if (encoded_byte & 128) == 0:
            break
            
    return (value, index - start)


def publish_size(topic_len: int, payload_len: int, qos: int) -> int:
    """Tổng số byte gói PUBLISH (MQTT 3.1.1). Nhớ: RL có thể 2 byte khi phần còn lại > 127."""
    # TODO 4
    packet_id_size = 2 if qos > 0 else 0
    # Remaining Length = 2 byte chứa độ dài topic + độ dài chuỗi topic + Packet ID (nếu có) + Payload
    rl = 2 + topic_len + packet_id_size + payload_len
    # Kích thước PUBLISH = Fixed Header (1 byte) + số byte biểu diễn RL + giá trị RL
    return 1 + rl_nbytes(rl) + rl


def exchange_bytes(topic_len: int, payload_len: int, qos: int) -> int:
    """Tổng byte ở tầng MQTT cho một lần publish, gồm các gói xác nhận (QoS 1: PUBACK; QoS 2: PUBREC+PUBREL+PUBCOMP)."""
    # TODO 5
    pub_bytes = publish_size(topic_len, payload_len, qos)
    if qos == 0:
        return pub_bytes
    elif qos == 1:
        # Cộng thêm PUBACK (4 byte)
        return pub_bytes + 4
    elif qos == 2:
        # Cộng thêm PUBREC (4 byte), PUBREL (4 byte), PUBCOMP (4 byte) = 12 byte
        return pub_bytes + 12
    return pub_bytes


def n_packets(qos: int) -> int:
    return {0: 1, 1: 2, 2: 4}[qos]


def wire_bytes(topic_len: int, payload_len: int, qos: int, ip_tcp: int = 40) -> int:
    """Byte trên dây nếu mỗi gói MQTT đi trong một đoạn TCP riêng (IPv4 20 + TCP 20)."""
    return exchange_bytes(topic_len, payload_len, qos) + ip_tcp * n_packets(qos)


def _selftest():
    # Số liệu bài giảng Ch.7: topic factory/hcm01/pump/p07/telemetry (32 B), payload struct 52 B
    assert rl_nbytes(127) == 1 and rl_nbytes(128) == 2 and rl_nbytes(16383) == 2 and rl_nbytes(16384) == 3
    assert encode_rl(163) == bytes([0xA3, 0x01]) and encode_rl(0) == b"\x00" and encode_rl(127) == b"\x7f"
    assert decode_rl(bytes([0x30, 0xA3, 0x01])) == (163, 2)
    assert decode_rl(bytes([0x30, 0xA3])) is None
    for n in (0, 1, 127, 128, 200, 16383, 16384, 2_097_151, 2_097_152, 268_435_455):
        assert decode_rl(b"\x30" + encode_rl(n)) == (n, rl_nbytes(n)), n
    assert [publish_size(32, 52, q) for q in (0, 1, 2)] == [88, 90, 90]
    assert [exchange_bytes(32, 52, q) for q in (0, 1, 2)] == [88, 94, 102]
    assert [wire_bytes(32, 52, q) for q in (0, 1, 2)] == [128, 174, 262]
    assert publish_size(32, 129, 0) == 166
    print("Tự kiểm tra: ĐẠT (khớp số liệu Ch.7)")


if __name__ == "__main__":
    try:
        _selftest()
    except NotImplementedError as e:
        print("Chưa hoàn thành:", e)
        raise SystemExit(1)
    import config
    from payload_codec import json_len
    tl = len(config.T_TELE)
    pj = json_len(config.PREC)
    print(f"\nCấu hình của bạn: topic {config.T_TELE} = {tl} B; JSON (PREC={config.PREC}) = {pj} B; struct = 52 B")
    print(f"{'payload':10s} {'QoS':>3s} {'RL':>4s} {'RLbytes':>7s} {'PUBLISH':>7s} {'MQTT tổng':>9s} {'+TCP/IP':>8s}")
    for name, p in (("struct", 52), ("JSON", pj)):
        for q in (0, 1, 2):
            rl = 2 + tl + (2 if q else 0) + p
            print(f"{name:10s} {q:>3d} {rl:>4d} {rl_nbytes(rl):>7d} {publish_size(tl, p, q):>7d} "
                  f"{exchange_bytes(tl, p, q):>9d} {wire_bytes(tl, p, q):>8d}")

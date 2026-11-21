"""Proxy TCP "nghe lén" MQTT để ĐẾM BYTE từng gói (Lab TH3 – CE339).

Client (paho, MQTT Explorer…) kết nối tới proxy thay vì broker; proxy chuyển tiếp nguyên vẹn mọi byte
và tách luồng byte thành từng gói MQTT nhờ header cố định (byte 1 + Remaining Length), ghi ra màn hình và CSV.

  python3 mqtt_proxy.py                       # nghe 127.0.0.1:1884 -> test.mosquitto.org:1883
  python3 mqtt_proxy.py --up-host 127.0.0.1 --up-port 1883 --csv proxy_log.csv
  MQTT_HOST=127.0.0.1 MQTT_PORT=1884 python3 qos_rtt.py      # cho client đi qua proxy
  python3 mqtt_proxy.py --raw --up-port 8883  # TLS: chỉ đếm byte thô (không đọc được nội dung đã mã hoá)

CHỈ client chạy trên máy bạn đi qua được proxy này. ESP32 trong Wokwi (Public Gateway) KHÔNG kết nối được tới
127.0.0.1/LAN của bạn -> byte của chính ESP32 đo bằng file PCAP tải từ Wokwi (biểu tượng Wi-Fi -> Download PCAP file).

Giả định MQTT 3.1.1 (paho mặc định, PubSubClient). Với MQTT 5, độ dài payload in ra sẽ sai vì có thêm thuộc tính.
"""
import argparse
import asyncio
import csv
import time

from mqtt_bytes import PACKET_NAMES, decode_rl

STATS = {}          # (hướng, loại gói) -> [số gói, tổng byte]


def parse_packet(buf):
    """Nếu buf bắt đầu bằng một gói MQTT đầy đủ, trả về (dict thông tin, tổng số byte của gói); nếu chưa đủ -> None."""
    if len(buf) < 2:
        return None
    r = decode_rl(buf, 1)
    if r is None:
        return None
    rl, k = r
    total = 1 + k + rl
    if len(buf) < total:
        return None
    ptype, flags = buf[0] >> 4, buf[0] & 0x0F
    info = {"type": PACKET_NAMES.get(ptype, f"?{ptype}"), "flags": flags, "rl": rl, "rl_bytes": k, "total": total,
            "topic": "", "qos": "", "retain": "", "dup": "", "payload": ""}
    body = bytes(buf[1 + k:total])
    if ptype == 3:  # PUBLISH
        qos = (flags >> 1) & 3
        tl = int.from_bytes(body[0:2], "big")
        info.update(qos=qos, retain=flags & 1, dup=(flags >> 3) & 1,
                    topic=body[2:2 + tl].decode("utf-8", "replace"),
                    payload=rl - 2 - tl - (2 if qos else 0))
    elif ptype == 1:  # CONNECT: in Keep Alive và cờ Will cho tiện quan sát
        pl = int.from_bytes(body[0:2], "big")
        cflags = body[2 + pl + 1]
        ka = int.from_bytes(body[2 + pl + 2:2 + pl + 4], "big")
        info["topic"] = (f"KA={ka}s will={(cflags >> 2) & 1} willQoS={(cflags >> 3) & 3} "
                         f"willRetain={(cflags >> 5) & 1} clean={(cflags >> 1) & 1}")
    return info, total


class Logger:
    def __init__(self, path):
        self.f = open(path, "w", newline="", encoding="utf-8") if path else None
        self.w = csv.writer(self.f) if self.f else None
        if self.w:
            self.w.writerow(["t", "conn", "dir", "type", "total", "rl", "rl_bytes", "qos", "retain", "dup", "payload", "topic"])
        self.t0 = time.time()

    def log(self, conn, direction, p):
        t = time.time() - self.t0
        key = (direction, p["type"])
        s = STATS.setdefault(key, [0, 0])
        s[0] += 1
        s[1] += p["total"]
        print(f"{t:8.3f} #{conn} {direction} {p['type']:<11s} {p['total']:5d} B  RL={p['rl']:<4d}({p['rl_bytes']}B) "
              f"q={p['qos']} r={p['retain']} {('pl=' + str(p['payload'])) if p['payload'] != '' else ''} {p['topic']}")
        if self.w:
            self.w.writerow([f"{t:.4f}", conn, direction, p["type"], p["total"], p["rl"], p["rl_bytes"], p["qos"],
                             p["retain"], p["dup"], p["payload"], p["topic"]])
            self.f.flush()

    def raw(self, conn, direction, n):
        key = (direction, "RAW")
        s = STATS.setdefault(key, [0, 0])
        s[0] += 1
        s[1] += n


async def pipe(reader, writer, direction, conn, logger, raw):
    buf = bytearray()
    try:
        while True:
            data = await reader.read(65536)
            if not data:
                break
            writer.write(data)
            await writer.drain()
            if raw:
                logger.raw(conn, direction, len(data))
                continue
            buf += data
            while True:
                r = parse_packet(buf)
                if r is None:
                    break
                info, n = r
                del buf[:n]
                logger.log(conn, direction, info)
    except (ConnectionResetError, BrokenPipeError, asyncio.IncompleteReadError):
        pass
    finally:
        try:
            writer.close()
        except Exception:
            pass


def print_stats():
    print("\n=== Thống kê theo loại gói ===")
    for (direction, t), (n, b) in sorted(STATS.items()):
        print(f"{direction} {t:<11s} {n:6d} gói {b:9d} B  (trung bình {b / n:7.1f} B/gói)")


async def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--listen", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=1884)
    ap.add_argument("--up-host", default="test.mosquitto.org")
    ap.add_argument("--up-port", type=int, default=1883)
    ap.add_argument("--csv", default="proxy_log.csv")
    ap.add_argument("--raw", action="store_true", help="chỉ đếm byte thô (dùng cho TLS)")
    a = ap.parse_args()
    logger = Logger(a.csv)
    counter = [0]

    async def handle(cr, cw):
        counter[0] += 1
        conn = counter[0]
        ur, uw = await asyncio.open_connection(a.up_host, a.up_port)
        print(f"--- kết nối #{conn} -> {a.up_host}:{a.up_port}")
        await asyncio.gather(pipe(cr, uw, "C->B", conn, logger, a.raw), pipe(ur, cw, "B->C", conn, logger, a.raw))
        print(f"--- đóng kết nối #{conn}")
        if STATS:
            print_stats()

    srv = await asyncio.start_server(handle, a.listen, a.port)
    print(f"Proxy MQTT nghe {a.listen}:{a.port} -> {a.up_host}:{a.up_port}  (Ctrl+C để dừng)")
    async with srv:
        await srv.serve_forever()


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print_stats()

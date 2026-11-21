"""Subscriber ghi nhật ký mọi bản tin của nút máy bơm (Lab TH3 – CE339).

  python3 sub_logger.py                 # subscribe ce339/<MSSV>/# với QoS 1
  python3 sub_logger.py --qos 0
In: thời điểm, topic, QoS nhận được, cờ retain, độ dài payload, kích thước gói PUBLISH tính theo công thức,
nội dung đã giải mã (JSON hoặc struct 52 B). Ghi thêm sub_log.csv (t_rx = đồng hồ THỰC của máy tính).
Thí nghiệm Last Will: khi nhận event "hang" rồi status "offline", in Δt giữa hai mốc (cùng đồng hồ máy tính).
"""
import argparse
import csv
import time

import paho.mqtt.client as mqtt

import config
from mqtt_bytes import publish_size
from payload_codec import decode_any


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--qos", type=int, default=1)
    ap.add_argument("--filter", default=f"ce339/{config.MSSV}/#")
    ap.add_argument("--csv", default="sub_log.csv")
    a = ap.parse_args()
    f = open(a.csv, "w", newline="", encoding="utf-8")
    w = csv.writer(f)
    w.writerow(["t_rx", "topic", "qos", "retain", "len", "publish_bytes", "ts", "fmt", "rms_x", "text"])

    def on_connect(c, u, flags, rc, props):
        print("Đã kết nối:", rc, "- subscribe", a.filter, "QoS", a.qos)
        c.subscribe(a.filter, qos=a.qos)

    t_hang = {}                                           # thời điểm nhận event "hang" (đồng hồ máy tính)

    def on_message(c, u, m):
        t = time.time()
        n = len(m.payload)
        pb = publish_size(len(m.topic), n, m.qos)
        ts, fmt, rx, txt = "", "", "", ""
        if m.topic.endswith("/telemetry"):
            try:
                o = decode_any(m.payload)
                ts, fmt, rx = o["ts"], o["fmt"], o["rms"][0]
                txt = f"{fmt} ts={ts} rms={[round(x, 5) for x in o['rms']]} kurt={[round(x, 3) for x in o['kurt']]}"
            except Exception as e:                       # payload lạ -> vẫn ghi
                txt = f"không giải mã được: {e}"
        else:
            txt = m.payload[:80].decode("utf-8", "replace")
            if m.topic.endswith("/event") and txt == "hang":
                t_hang["t"] = t
            if m.topic.endswith("/status") and "offline" in txt and "t" in t_hang and not m.retain:
                print(f"*** Δt từ 'hang' tới 'offline' = {t - t_hang.pop('t'):.1f} s (so với 1,5 × KA = {1.5 * config.KA:.1f} s)")
        print(f"{time.strftime('%H:%M:%S')} {m.topic} q={m.qos} r={int(m.retain)} len={n} PUBLISH={pb}B  {txt}")
        w.writerow([f"{t:.3f}", m.topic, m.qos, int(m.retain), n, pb, ts, fmt, rx, "" if ts != "" else txt[:40]])
        f.flush()

    cli = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=f"ce339-{config.MSSV}-py-sub")
    cli.on_connect, cli.on_message = on_connect, on_message
    cli.connect(config.BROKER, config.PORT, keepalive=30)
    try:
        cli.loop_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()

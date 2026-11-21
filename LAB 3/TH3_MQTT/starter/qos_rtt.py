"""Đo thời gian theo QoS bằng paho-mqtt (Lab TH3 – CE339).  MÃ KHUNG – SV hoàn thành các TODO.

Vì PubSubClient trên ESP32 chỉ publish được QoS 0, phần QoS 1/2 được đo từ máy tính bằng paho-mqtt (>= 2.0).
Đo 4 đại lượng, mỗi đại lượng N lần:
  tcp      : thời gian mở kết nối TCP tới broker (~ 1 RTT mạng)
  ack_q0/1/2: thời gian từ lúc gọi publish() tới khi paho báo "đã xong":
             QoS 0 = chỉ ghi vào socket (không có xác nhận); QoS 1 = nhận PUBACK; QoS 2 = nhận PUBCOMP
  loop_q0  : vòng lặp publish QoS 0 -> broker -> chính client này (đã subscribe cùng topic)
Mọi thời gian đo bằng time.perf_counter() của máy tính = THỜI GIAN THỰC.
Kết quả: in trung vị / trung bình / p95 và ghi rtt_py.csv.
  --echo    : đo RTT VÒNG QUA ESP32 TRÊN WOKWI (ESP32 phải đang chạy, cùng MSSV, cùng broker công cộng):
              Python -> broker -> Wokwi Public Gateway -> ESP32 -> broker -> Python; ghi rtt_echo.csv.
              (ESP32 trong Wokwi KHÔNG nói chuyện được với broker cục bộ -> không dùng MQTT_HOST=127.0.0.1 ở chế độ này)
  python3 qos_rtt.py            # N = 100
  python3 qos_rtt.py -n 20
  python3 qos_rtt.py --echo -n 50
"""
import argparse
import csv
import socket
import statistics
import threading
import time

import paho.mqtt.client as mqtt

import config

def summary(xs):
    """Trả về (trung vị, trung bình, p95, số mẫu) – đơn vị giữ nguyên như xs."""
    s = sorted(xs)
    n = len(s)
    if not s:
        return (float("nan"),) * 3 + (0,)
    # TODO 9: tính trung vị, trung bình và phân vị 95%
    median_val = statistics.median(s)
    mean_val = statistics.mean(s)
    p95_idx = round(0.95 * (n - 1))
    p95_val = s[p95_idx]
    
    return median_val, mean_val, p95_val, n

def measure_tcp(n):
    out = []
    for _ in range(n):
        t0 = time.perf_counter()
        s = socket.create_connection((config.BROKER, config.PORT), timeout=5)
        out.append((time.perf_counter() - t0) * 1e3)
        s.close()
        time.sleep(0.05)
    return out


def measure_ack(cli, qos, n, gap):
    """Đo thời gian publish() -> hoàn tất (PUBACK với QoS 1, PUBCOMP với QoS 2)."""
    out = []
    for i in range(n):
        # TODO 10: ghi thời điểm t0 = time.perf_counter(); publish lên config.T_RTT_PY + "/ack" với QoS = qos;
        #          chờ info.wait_for_publish(timeout=5); nếu info.is_published() thì thêm (thời gian đã trôi, ms) vào out;
        #          nghỉ gap giây giữa hai lần.  Hỏi: với QoS 0, "đã publish" nghĩa là gì?
        t0 = time.perf_counter()
        info = cli.publish(config.T_RTT_PY + "/ack", str(i), qos=qos)
        info.wait_for_publish(timeout=5)
        if info.is_published():
            dt_ms = (time.perf_counter() - t0) * 1e3
            out.append(dt_ms)
        time.sleep(gap)
    return out


def measure_loop(cli, n, gap, qos=0):
    """Publish seq lên T_RTT_PY (client đã subscribe) và đo tới khi nhận lại chính bản tin đó."""
    sent, got = {}, {}
    ev = threading.Event()

    def on_message(c, u, msg):
        if msg.topic != config.T_RTT_PY:
            return
        t = time.perf_counter()
        seq = int(msg.payload)
        if seq in sent and seq not in got:
            got[seq] = (t - sent[seq]) * 1e3
            if len(got) == n:
                ev.set()

    cli.on_message = on_message
    cli.subscribe(config.T_RTT_PY, qos=qos)
    time.sleep(1.0)                      # chờ SUBACK
    for i in range(n):
        sent[i] = time.perf_counter()    # ghi thời điểm TRƯỚC khi publish -> không có tranh chấp
        cli.publish(config.T_RTT_PY, str(i), qos=qos)
        time.sleep(gap)
    ev.wait(timeout=5)
    cli.unsubscribe(config.T_RTT_PY)
    lost = n - len(got)
    return [got[k] for k in sorted(got)], lost


def measure_echo(cli, n, gap):
    """(Đã cho sẵn) Publish seq lên T_PING; ESP32 trả nguyên payload lên T_PONG; đo tới khi nhận pong (đồng hồ thực)."""
    sent, got = {}, {}
    ev = threading.Event()

    def on_message(c, u, msg):
        if msg.topic != config.T_PONG:
            return
        t = time.perf_counter()
        try:
            seq = int(msg.payload)
        except ValueError:
            return
        if seq in sent and seq not in got:
            got[seq] = (t - sent[seq]) * 1e3
            if len(got) == n:
                ev.set()

    cli.on_message = on_message
    cli.subscribe(config.T_PONG, qos=0)
    time.sleep(1.0)
    for i in range(n):
        sent[i] = time.perf_counter()
        cli.publish(config.T_PING, str(i), qos=0)
        time.sleep(gap)
    ev.wait(timeout=10)
    cli.unsubscribe(config.T_PONG)
    return [got[k] for k in sorted(got)], n - len(got)


def run_echo(a):
    cli = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=f"ce339-{config.MSSV}-py-echo")
    cli.connect(config.BROKER, config.PORT, keepalive=30)
    cli.loop_start()
    t0 = time.perf_counter()
    while not cli.is_connected() and time.perf_counter() - t0 < 10:
        time.sleep(0.001)
    loop, lost_l = measure_loop(cli, min(a.n, 50), a.gap, qos=0)     # mốc so sánh: vòng Python -> broker -> Python
    echo, lost_e = measure_echo(cli, a.n, max(a.gap, 0.2))
    cli.loop_stop()
    cli.disconnect()
    out = a.csv if a.csv != "rtt_py.csv" else "rtt_echo.csv"
    with open(out, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["kind", "i", "ms"])
        for k, xs in (("loop_q0", loop), ("echo_esp32", echo)):
            for i, x in enumerate(xs):
                w.writerow([k, i, f"{x:.3f}"])
    print(f"{'đại lượng':10s} {'trung vị':>9s} {'TB':>9s} {'p95':>9s} {'n':>4s}  (ms, thời gian thực)")
    for k, xs in (("loop_q0", loop), ("echo_esp32", echo)):
        md, mn, p95, n = summary(xs)
        print(f"{k:10s} {md:9.2f} {mn:9.2f} {p95:9.2f} {n:4d}")
    print(f"mất: loop {lost_l}, echo {lost_e}/{a.n}")
    if not echo:
        print("Không nhận được pong nào: ESP32 đã chạy và online chưa? Cùng MSSV/PUMP_ID? Cùng broker công cộng?")
    else:
        print(f"echo − loop (trung vị) ≈ {summary(echo)[0] - summary(loop)[0]:.1f} ms ≈ chặng broker <-> Wokwi gateway <-> ESP32")
    print("Đã ghi", out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-n", type=int, default=100)
    ap.add_argument("--gap", type=float, default=0.1, help="khoảng cách giữa hai lần gửi (s)")
    ap.add_argument("--csv", default="rtt_py.csv")
    ap.add_argument("--echo", action="store_true", help="đo RTT vòng qua ESP32 (Wokwi) bằng rtt/ping -> rtt/pong")
    a = ap.parse_args()
    if a.echo:
        return run_echo(a)

    print(f"Broker {config.BROKER}:{config.PORT}, topic {config.T_RTT_PY}, N = {a.n}")
    res = {"tcp": measure_tcp(min(a.n, 20))}

    cli = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=f"ce339-{config.MSSV}-py-rtt")
    t0 = time.perf_counter()
    cli.connect(config.BROKER, config.PORT, keepalive=30)
    cli.loop_start()
    while not cli.is_connected() and time.perf_counter() - t0 < 10:
        time.sleep(0.001)
    res["connect"] = [(time.perf_counter() - t0) * 1e3]   # TCP + CONNECT/CONNACK
    for q in (0, 1, 2):
        res[f"ack_q{q}"] = measure_ack(cli, q, a.n, a.gap)
    res["loop_q0"], lost = measure_loop(cli, a.n, a.gap, qos=0)
    cli.loop_stop()
    cli.disconnect()

    with open(a.csv, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["kind", "i", "ms"])
        for k, xs in res.items():
            for i, x in enumerate(xs):
                w.writerow([k, i, f"{x:.3f}"])
    print(f"{'đại lượng':10s} {'trung vị':>9s} {'TB':>9s} {'p95':>9s} {'n':>4s}  (ms)")
    for k, xs in res.items():
        md, mn, p95, n = summary(xs)
        print(f"{k:10s} {md:9.2f} {mn:9.2f} {p95:9.2f} {n:4d}")
    print(f"loop_q0 mất {lost}/{a.n} bản tin")
    q1 = summary(res["ack_q1"])[0]
    q2 = summary(res["ack_q2"])[0]
    if q1 > 0:
        print(f"Tỉ số trung vị QoS2/QoS1 = {q2 / q1:.2f} (dự đoán ≈ 2)")
    print("Đã ghi", a.csv)


if __name__ == "__main__":
    main()

"""Gửi lệnh xuống nút máy bơm qua topic .../cmd (Lab TH3 – CE339).

  python3 send_cmd.py --val 1              # bật LED (lệnh lũy đẳng "đặt LED = 1"), QoS 1, KHÔNG retain
  python3 send_cmd.py --val 0 --retain     # THÍ NGHIỆM lỗi: lệnh retained -> nút khởi động lại chạy lại lệnh cũ
  python3 send_cmd.py --clear              # xoá bản tin retained trên topic cmd (payload rỗng + retain)
"""
import argparse
import json
import time
import uuid

import paho.mqtt.client as mqtt

import config


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--op", default="led")
    ap.add_argument("--val", type=int, default=1)
    ap.add_argument("--qos", type=int, default=1)
    ap.add_argument("--retain", action="store_true")
    ap.add_argument("--clear", action="store_true")
    a = ap.parse_args()
    cli = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=f"ce339-{config.MSSV}-py-cmd")
    cli.connect(config.BROKER, config.PORT, keepalive=30)
    cli.loop_start()
    if a.clear:
        info = cli.publish(config.T_CMD, b"", qos=1, retain=True)
        print("Xoá retained trên", config.T_CMD)
    else:
        cmd = {"id": "c" + uuid.uuid4().hex[:6], "op": a.op, "val": a.val}
        payload = json.dumps(cmd, separators=(",", ":"))
        t0 = time.perf_counter()
        info = cli.publish(config.T_CMD, payload, qos=a.qos, retain=a.retain)
        info.wait_for_publish(5)
        print(f"Gửi {payload} -> {config.T_CMD} QoS {a.qos} retain={a.retain}; "
              f"hoàn tất sau {(time.perf_counter() - t0) * 1e3:.1f} ms")
    info.wait_for_publish(5)
    cli.loop_stop()
    cli.disconnect()


if __name__ == "__main__":
    main()

# Cấu hình chung cho các script Lab TH3 (CE339).
# Sửa MSSV của bạn ở đây. LƯU Ý: ESP32 trong Wokwi chỉ tới được broker CÔNG CỘNG (Public Gateway không vào
# được localhost/LAN) -> khi chạy cùng ESP32, để BROKER mặc định test.mosquitto.org. Broker/proxy cục bộ chỉ cho client Python.
# Có thể ghi đè broker bằng biến môi trường, ví dụ khi đi qua proxy đếm byte:
#   MQTT_HOST=127.0.0.1 MQTT_PORT=1884 python3 qos_rtt.py
import os

MSSV = os.environ.get("MSSV", "23520469")          # <-- sửa thành MSSV của bạn (8 chữ số), ví dụ "23520147"
if not (MSSV.isdigit() and len(MSSV) == 8):
    raise SystemExit("Hãy sửa MSSV (8 chữ số) trong config.py trước khi chạy")
BROKER = os.environ.get("MQTT_HOST", "broker.hivemq.com")
PORT = int(os.environ.get("MQTT_PORT", "1883"))

d = int(MSSV[-1])            # chữ số cuối
c = int(MSSV[-2])            # chữ số áp cuối
PUMP_ID = f"p{c}{d}"         # ví dụ 23520147 -> p47
BASE = f"ce339/{MSSV}/pump/{PUMP_ID}"
T_TELE = BASE + "/telemetry"
T_STATUS = BASE + "/status"
T_CMD = BASE + "/cmd"
T_RTT_PY = BASE + "/rtt/py"  # topic riêng cho đo RTT bằng Python
T_PING = BASE + "/rtt/ping"  # Python -> ESP32 (ESP32 trả lời ngay lên T_PONG)
T_PONG = BASE + "/rtt/pong"
T_EVENT = BASE + "/event"    # ESP32 gửi "hang" ngay trước khi treo (mốc cho thí nghiệm Last Will)
PREC = 2 + d % 4             # số chữ số thập phân trong JSON
KA = 10 + c                  # Keep Alive của ESP32 (giây)
RTT0_MS = 30 + 10 * d        # RTT giả định dùng trong phiếu dự đoán (ms)

if __name__ == "__main__":
    print(f"MSSV={MSSV} d={d} c={c} PUMP_ID={PUMP_ID} PREC={PREC} KA={KA}s RTT0={RTT0_MS}ms")
    print("telemetry topic:", T_TELE, f"({len(T_TELE)} ký tự)")
    print("broker:", BROKER, PORT)

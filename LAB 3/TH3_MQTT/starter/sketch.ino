// =====================================================================================
// CE339 – Lab TH3: MQTT – thiết kế topic, QoS, LWT, đo đạc          (MÃ KHUNG – có TODO)
// ESP32 (Wokwi) + PubSubClient. Nút máy bơm mô phỏng: biến trở = mức rung, LED = cơ cấu chấp hành.
// Lưu ý: PubSubClient chỉ PUBLISH được QoS 0; SUBSCRIBE được QoS 0/1; Will được đặt QoS 1.
// Lệnh qua Serial Monitor: j = gửi JSON, b = gửi struct 52 B, r = đo RTT 100 lần, c = kết nối lại
// Nút HANG (GPIO18): treo firmware (không gọi mqtt.loop) -> quan sát Last Will
// Nút DISC (GPIO19): ngắt kết nối "đàng hoàng" (DISCONNECT) -> Will KHÔNG được phát
// MẠNG WOKWI: ESP32 ra Internet qua Wokwi Public Gateway -> CHỈ tới được broker CÔNG CỘNG (test.mosquitto.org).
//   Gateway không vào được localhost/LAN của máy bạn và không nhận kết nối vào -> không trỏ BROKER về máy mình.
// THỜI GIAN: millis()/micros() là thời gian MÔ PHỎNG. Khi mô phỏng chạy < 100% tốc độ, thời gian chờ mạng (thực)
//   bị "co" lại theo đồng hồ mô phỏng -> RTT/thời gian kết nối đo trên ESP32 chỉ để tham khảo; RTT chuẩn đo ở phía Python.
// =====================================================================================
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <time.h>
#include <math.h>
#include "ca_cert.h"

// ------------------------- CÁ NHÂN HOÁ THEO MSSV (d = chữ số cuối, c = áp cuối) -------------
#define MSSV     "SUA_MSSV" // TODO 0: MSSV 8 chữ số của bạn, ví dụ "23520147"
#define PUMP_ID  "pXX"      // TODO 0: p<c><d>, ví dụ MSSV 23520147 -> "p47"
#define PREC     4          // TODO 0: số chữ số thập phân JSON = 2 + (d mod 4)
#define KA_S     10         // TODO 0: Keep Alive (s) = 10 + c
#define USE_TLS  0          // 0: cổng 1883 (không mã hoá); 1: TLS cổng 8883 (cần ca_cert.h)
// ----------------------------------------------------------------------------------------------

const char* WIFI_SSID = "Wokwi-GUEST";
const char* BROKER    = "test.mosquitto.org";
const int PIN_POT = 34, PIN_LED = 26, PIN_HANG = 18, PIN_DISC = 19;
const uint32_t T_PUB_MS = 1000;     // chu kỳ gửi telemetry
const int RTT_N = 100;              // số lần đo RTT
const uint32_t RTT_GAP_MS = 200;    // khoảng cách giữa hai lần ping

WiFiClient netPlain;
WiFiClientSecure netTls;
PubSubClient mqtt;

char T_TELE[64], T_STATUS[64], T_CMD[64], T_RTT[64];
char T_EVENT[64], T_PING[64], T_PONG[64];   // topic phụ cho đo đạc (đã cho sẵn, xem buildExtraTopics)
char clientId[48];
bool fmtJson = true;          // định dạng payload đang dùng
bool stayOffline = false;     // sau khi bấm DISC thì không tự kết nối lại
float feat[12];               // rms[3], pk[3], kurt[3], eb[3]
uint32_t lastPub = 0, lastRetry = 0;

// Đo RTT: ESP32 publish vào T_RTT (QoS 0) và đã subscribe chính topic đó -> broker gửi lại
bool rttRunning = false;
int rttSent = 0, rttGot = 0;
uint32_t rttT0[RTT_N];
uint32_t rttNext = 0, rttEnd = 0;
float rttSum = 0, rttMin = 1e9, rttMax = 0;

struct __attribute__((packed)) TeleMsg {   // phải khớp payload_codec.py: '<I12f'
  uint32_t ts;
  float f[12];
};
static_assert(sizeof(TeleMsg) == 52, "TeleMsg phai dung 52 byte");

// ----------------------------- Tín hiệu rung mô phỏng (đã cho sẵn) -----------------------------
// Wokwi không có rung thật: sinh 1000 mẫu/trục (fs = 1 kHz, 1 s) gồm sin 25 Hz biên độ theo biến trở
// + nhiễu Gauss, rồi tính 4 đặc trưng/trục như Ch.9. Mọi đặc trưng được kẹp trong [0; 9,99].
static float gauss() {
  float u1 = (random(1, 1000000)) / 1e6f, u2 = (random(0, 1000000)) / 1e6f;
  return sqrtf(-2.0f * logf(u1)) * cosf(2.0f * PI * u2);
}
static float clampf(float v) { return v < 0 ? 0 : (v > 9.99f ? 9.99f : v); }

void computeFeatures(float A) {
  const int NS = 1000;
  const float fs = 1000.0f, f0 = 25.0f;
  const float amp[3] = {A, 0.6f * A, 0.3f * A};
  for (int ax = 0; ax < 3; ax++) {
    double s2 = 0, s4 = 0, re = 0, im = 0;
    float pk = 0;
    for (int n = 0; n < NS; n++) {
      float ph = 2.0f * PI * f0 * n / fs;
      float x = amp[ax] * sinf(ph + ax) + 0.02f * gauss();
      double x2 = (double)x * x;
      s2 += x2; s4 += x2 * x2;
      if (fabsf(x) > pk) pk = fabsf(x);
      re += x * cosf(ph); im -= x * sinf(ph);
    }
    double m2 = s2 / NS, m4 = s4 / NS;
    feat[0 + ax] = clampf(sqrt(m2));                          // rms
    feat[3 + ax] = clampf(pk);                                // đỉnh
    feat[6 + ax] = clampf(m2 > 0 ? m4 / (m2 * m2) : 0);       // kurtosis
    feat[9 + ax] = clampf(2.0 * (re * re + im * im) / ((double)NS * NS));  // năng lượng tại 25 Hz
  }
}

uint32_t nowTs() {                 // giây Unix từ NTP; nếu chưa có NTP vẫn giữ 10 chữ số
  time_t t = time(nullptr);
  if (t < 1700000000) return 1790000000UL + millis() / 1000;
  return (uint32_t)t;
}

// ----------------------------------- TODO 1: cây topic -----------------------------------------
void buildTopics() {
  // TODO 1a: dùng snprintf tạo 4 topic theo cây ce339/<MSSV>/pump/<PUMP_ID>/<loại>:
  //   T_TELE  = .../telemetry      T_STATUS = .../status
  //   T_CMD   = .../cmd            T_RTT    = .../rtt/esp   (topic đo RTT của ESP32)
  snprintf(T_TELE, sizeof T_TELE, "ce339/%s/pump/%s/telemetry", MSSV, PUMP_ID);   // mẫu cho topic đầu tiên
  // ... viết tiếp 3 topic còn lại ...

  // TODO 1b: ClientID phải DUY NHẤT. Gợi ý: "ce339-<MSSV>-" + 6 chữ số hex cuối của ESP.getEfuseMac()
  snprintf(clientId, sizeof clientId, "ce339-%s", MSSV);   // <- sửa lại cho duy nhất
}

// --------------------------- Topic phụ cho đo đạc (đã cho sẵn) --------------------------------
// event : ESP32 báo "hang" ngay trước khi treo -> Python lấy mốc thời gian THỰC cho thí nghiệm Last Will
// rtt/ping -> rtt/pong : Python gửi ping, ESP32 trả pong ngay -> Python đo RTT vòng qua ESP32 bằng đồng hồ thực
void buildExtraTopics() {
  snprintf(T_EVENT, sizeof T_EVENT, "ce339/%s/pump/%s/event", MSSV, PUMP_ID);
  snprintf(T_PING, sizeof T_PING, "ce339/%s/pump/%s/rtt/ping", MSSV, PUMP_ID);
  snprintf(T_PONG, sizeof T_PONG, "ce339/%s/pump/%s/rtt/pong", MSSV, PUMP_ID);
}

// ------------------------------ TODO 2: kết nối có Last Will ----------------------------------
bool connectMqtt() {
  uint32_t t0 = millis();
  // TODO 2a: gọi mqtt.connect(...) có Last Will: topic T_STATUS, QoS 1, retain = true, nội dung "offline".
  //          (xem các phiên bản hàm connect() của PubSubClient)
  bool ok = mqtt.connect(clientId);          // <- thay bằng lời gọi có Will
  uint32_t dt = millis() - t0;                 // thời gian MÔ PHỎNG (xem ghi chú đầu tệp)
  Serial.printf("CONNECT,%s,%lu ms,state=%d\n", USE_TLS ? "TLS8883" : "TCP1883", (unsigned long)dt, mqtt.state());
  if (!ok) return false;
  // TODO 2b: publish "online" lên T_STATUS với cờ retain phù hợp (vì sao?)
  // TODO 2c: subscribe T_CMD với QoS 1 và T_RTT với QoS 0
  mqtt.subscribe(T_PING, 0);                  // (đã cho sẵn) nhận ping của qos_rtt.py --echo
  return true;
}

// ---------------------------- TODO 3: đóng gói và gửi telemetry --------------------------------
void publishTelemetry() {
  uint32_t ts = nowTs();
  bool ok = false;
  int len = 0;
  if (fmtJson) {
    char js[256];
    // TODO 3a: tạo JSON gọn ĐÚNG định dạng (khớp payload_codec.encode_json):
    //   {"ts":1790000000,"rms":[a,b,c],"pk":[a,b,c],"kurt":[a,b,c],"eb":[a,b,c]}
    //   mỗi số in với PREC chữ số thập phân: dùng "%.*f" với tham số PREC, feat[i]
    len = snprintf(js, sizeof js, "{\"ts\":%lu}", (unsigned long)ts);   // <- mới có ts, viết tiếp
    ok = mqtt.publish(T_TELE, js, false);
  } else {
    // TODO 3b: điền struct TeleMsg (ts + 12 float) và publish dạng nhị phân:
    //   mqtt.publish(topic, (const uint8_t*)&m, sizeof m, false)
  }
  Serial.printf("PUB,%s,%d B,ok=%d,rms_x=%.4f\n", fmtJson ? "json" : "struct", len, ok, feat[0]);
}

// ------------------------- TODO 4: xử lý bản tin đến (RTT và lệnh) -----------------------------
void onMessage(char* topic, byte* payload, unsigned int length) {
  char buf[160];
  unsigned int n = length < sizeof(buf) - 1 ? length : sizeof(buf) - 1;
  memcpy(buf, payload, n);
  buf[n] = 0;                                  // payload KHÔNG có ký tự kết thúc chuỗi -> tự thêm
  if (strcmp(topic, T_PING) == 0) {            // (đã cho sẵn) trả pong NGAY, dùng bản sao buf
    mqtt.publish(T_PONG, (const uint8_t*)buf, n, false);   // (không dùng payload: bộ đệm bị ghi đè khi publish)
    return;
  }
  if (strcmp(topic, T_RTT) == 0) {
    // TODO 4a: seq = atoi(buf); nếu 0 <= seq < RTT_N: ms = (micros() - rttT0[seq]) / 1000.0  (thời gian MÔ PHỎNG)
    //          cập nhật rttGot, rttSum, rttMin, rttMax và in "RTT,<seq>,<ms>"
    return;
  }
  if (strcmp(topic, T_CMD) == 0) {
    // TODO 4b: lệnh dạng {"id":"c1a2b3","op":"led","val":1}. Nếu payload rỗng (n == 0) -> bỏ qua.
    //          Tìm "\"val\":" bằng strstr, đọc số bằng atoi, đặt LED (digitalWrite PIN_LED), in "CMD,..." kèm millis()
  }
}

// ------------------------------ TODO 5: ngắt kết nối đàng hoàng --------------------------------
void gracefulDisconnect() {
  // TODO 5: khi ngắt kết nối "đàng hoàng", broker HUỶ Will. Cần làm gì để dashboard vẫn biết nút đã offline?
  //         Sau đó gọi mqtt.disconnect(); đặt stayOffline = true.
  mqtt.disconnect();
  stayOffline = true;
  Serial.printf("DISC tai %lu ms (gui 'c' de ket noi lai)\n", (unsigned long)millis());
}

// ----------------------------------- Đo RTT (đã cho sẵn) --------------------------------------
void rttStart() {
  rttRunning = true; rttSent = 0; rttGot = 0; rttSum = 0; rttMin = 1e9; rttMax = 0;
  rttNext = millis(); rttEnd = 0;
  Serial.println("RTT,seq,ms   (bat dau)");
}
void rttStep() {
  if (!rttRunning) return;
  uint32_t now = millis();
  if (rttSent < RTT_N && now >= rttNext) {
    char p[12];
    snprintf(p, sizeof p, "%d", rttSent);
    rttT0[rttSent] = micros();
    mqtt.publish(T_RTT, p, false);
    rttSent++;
    rttNext = now + RTT_GAP_MS;
    if (rttSent == RTT_N) rttEnd = now + 3000;
  }
  if (rttSent == RTT_N && now >= rttEnd) {
    rttRunning = false;
    Serial.printf("RTT_SUMMARY,got=%d/%d,mean=%.2f,min=%.2f,max=%.2f ms\n",
                  rttGot, RTT_N, rttGot ? rttSum / rttGot : 0, rttMin, rttMax);
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_HANG, INPUT_PULLUP);
  pinMode(PIN_DISC, INPUT_PULLUP);
  randomSeed(analogRead(PIN_POT) + micros());
  Serial.print("Ket noi Wi-Fi");
  WiFi.begin(WIFI_SSID, "", 6);
  while (WiFi.status() != WL_CONNECTED) { delay(200); Serial.print("."); }
  Serial.println(" OK");
  configTime(0, 0, "pool.ntp.org");
  buildTopics();
  buildExtraTopics();
  Serial.printf("ClientID=%s\nTELE=%s (%d ky tu)\n", clientId, T_TELE, (int)strlen(T_TELE));
#if USE_TLS
  netTls.setCACert(CA_CERT);
  mqtt.setClient(netTls);
  mqtt.setServer(BROKER, 8883);
#else
  mqtt.setClient(netPlain);
  mqtt.setServer(BROKER, 1883);
#endif
  mqtt.setKeepAlive(KA_S);
  mqtt.setBufferSize(512);
  mqtt.setCallback(onMessage);
}

void loop() {
  uint32_t now = millis();
  if (!stayOffline && !mqtt.connected() && now - lastRetry > 2000) {
    lastRetry = now;
    connectMqtt();
  }
  mqtt.loop();

  if (digitalRead(PIN_HANG) == LOW) {
    // Gói cuối cùng broker nhận từ nút: bản tin "hang" -> Python lấy mốc (đồng hồ THỰC) để đo thời gian tới Will
    mqtt.publish(T_EVENT, "hang", false);
    Serial.printf("HANG tai %lu ms (thoi gian mo phong) – da gui event 'hang', ngung goi mqtt.loop()\n", (unsigned long)millis());
    while (true) delay(100);           // TCP vẫn mở nhưng không còn gói MQTT nào
  }
  if (digitalRead(PIN_DISC) == LOW && mqtt.connected()) gracefulDisconnect();

  while (Serial.available()) {
    char ch = Serial.read();
    if (ch == 'j') fmtJson = true;
    if (ch == 'b') fmtJson = false;
    if (ch == 'r' && mqtt.connected()) rttStart();
    if (ch == 'c') { stayOffline = false; lastRetry = 0; }
  }

  if (rttRunning) { rttStep(); return; }          // tạm dừng telemetry khi đo RTT
  if (mqtt.connected() && now - lastPub >= T_PUB_MS) {
    lastPub = now;
    float A = 0.05f + 0.95f * analogRead(PIN_POT) / 4095.0f;   // biên độ rung (g)
    computeFeatures(A);
    publishTelemetry();
  }
}

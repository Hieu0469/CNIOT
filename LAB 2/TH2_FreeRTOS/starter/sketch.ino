// =====================================================================================
// CE339 – Lab TH2: Lấy mẫu định kỳ với FreeRTOS
// MÃ KHUNG cho sinh viên – tìm các chỗ "TODO"
// Phần cứng (Wokwi): ESP32 DevKit C v4 + nút nhấn (GPIO4) + LED (GPIO2) + logic analyzer (D0 <- GPIO18; D1..D3 <- GPIO19, 23, 25)
// Tệp đi kèm: ringbuf.h/.c, stats.h/.c
// Thí nghiệm 1: jitter lấy mẫu – delayMicroseconds / delay() / vTaskDelayUntil, có/không tải nền
// Thí nghiệm 2: đảo ngược độ ưu tiên H–M–L – semaphore nhị phân vs mutex
// Thí nghiệm 3: watchdog phần mềm (nhịp tim) – mô phỏng task treo bằng nút nhấn
// Lưu ý Wokwi: micros()/millis()/tick là THỜI GIAN MÔ PHỎNG (có thể chậm hơn thời gian thực); logic analyzer ghi VCD
//   cũng theo thời gian mô phỏng -> hai cách đo so sánh trực tiếp được với nhau.
// =====================================================================================
#include <Arduino.h>
#include "ringbuf.h"
#include "stats.h"

// ----------------------------- 1. THAM SỐ CÁ NHÂN (SV sửa MSSV) ---------------------
#define MSSV 23520147UL
#define THI_NGHIEM 1      // 1: jitter | 2: đảo ngược ưu tiên | 3: watchdog
#define CHE_DO 'C'        // TN1: 'A' delayMicroseconds(T0) | 'B' delay(T0/1000) | 'C' vTaskDelayUntil
#define TAI_NEN 0         // TN1: 0 không tải | 1 có task "Wi-Fi giả lập" ưu tiên cao chen ngang
#define KHOA 1            // TN2: 0 semaphore nhị phân | 1 mutex

const uint32_t D = MSSV % 10;
const uint32_t C = (MSSV / 10) % 10;
const uint32_t T0_US = 1000UL * (1 + D % 3);       // chu kỳ lấy mẫu danh định
const uint32_t TX_US = 250 + 50 * C;               // thời gian "đọc cảm biến + xử lý" mỗi mẫu (tốn CPU)
const uint32_t TB_US = 500UL * (2 + D % 4);        // tải nền: mỗi PB_MS chiếm CPU TB_US
const uint32_t PB_MS = 100;
const uint32_t CL_MS = 25 + 2 * C;                 // TN2: đoạn găng của L
const uint32_t CM_MS = 30 + 5 * D;                 // TN2: thời gian chạy của M
const uint32_t WDT_TIMEOUT_MS = 1500 + 500 * (C % 3);
const uint32_t WDT_CHECK_MS = 500;

#define CORE_SAMPLE 1     // lấy mẫu + tải nền + TN2 chạy trên core 1
#define CORE_OTHER  0     // xử lý, in, watchdog chạy trên core 0
#define PIN_PROBE 18      // đảo mức mỗi mẫu -> logic analyzer D0 (Bước 3b: đo chu kỳ trên tệp VCD)
#define PIN_D1    19      // (nâng cao 1) logic analyzer D1..D3: SV tự dùng để đánh dấu khoảng thời gian
#define PIN_D2    23
#define PIN_D3    25
#define PIN_LED   2
#define PIN_BTN   4
#define N_SKIP    5       // bỏ 5 mẫu đầu
#define N_LOG     10100   // tối đa số nhãn thời gian lưu trong 10 s (thống kê 10 s tính trên ESP32)
#define N_IN      2000    // chỉ IN 2000 nhãn đầu ra Serial (đủ vẽ hình, dễ chép từ Serial Monitor);
                          // chuỗi đầy đủ lấy từ tệp VCD của logic analyzer (Bước 3b)

// ----------------------------- 2. CÔNG CỤ DÙNG CHUNG (cho sẵn) -------------------------
volatile uint32_t g_sink;
float g_iter_per_us = 1.0f;
void burn_us(uint32_t us) {                        // chiếm CPU THẬT ~us micro giây (bị chen ngang thì kéo dài ra)
  uint32_t n = (uint32_t)(us * g_iter_per_us);
  for (uint32_t i = 0; i < n; i++) g_sink = g_sink + i;
}
void calibrate_burn() {
  const uint32_t N = 200000;
  float best = 0;
  for (int k = 0; k < 3; k++) {
    uint32_t t = micros();
    for (uint32_t i = 0; i < N; i++) g_sink = g_sink + i;
    float ipu = N / (float)(micros() - t);
    if (ipu > best) best = ipu;                    // lấy lần ít bị ngắt nhất
  }
  g_iter_per_us = best;
}

ringbuf_t g_rb;
volatile uint32_t g_hb_ms[2];                      // nhịp tim: [0] task lấy mẫu, [1] task xử lý
volatile bool     g_hang = false;
volatile uint32_t g_hang_ms = 0, g_first_ovf_ms = 0, g_rb_max = 0;
uint32_t g_tlog[N_LOG];
volatile uint32_t g_nlog = 0;
TaskHandle_t hSample = NULL, hProcess = NULL, hPrint = NULL, hLoad = NULL;

typedef struct { uint8_t final_; uint32_t n; float mean, sd, mn, mx; uint32_t ovf, rbmax; int32_t lech_us; } snap_t;
QueueHandle_t g_snapq;

// ----------------------------- 3. THÍ NGHIỆM 1 & 3: các task --------------------------
void taskSample(void *arg) {
  TickType_t last = xTaskGetTickCount();
  uint32_t seq = 0;
  for (;;) {
    uint32_t t = micros();
    digitalWrite(PIN_PROBE, seq & 1);
    sample_t s = { seq++, t };
    if (!rb_push(&g_rb, s) && g_first_ovf_ms == 0) g_first_ovf_ms = millis();
    g_hb_ms[0] = millis();
    burn_us(TX_US);                                // giả lập đọc I2C + xử lý
    // TODO(2): chờ theo CHE_DO
    //   'A': chờ THÊM T0_US micro giây bằng delayMicroseconds()
    //   'B': chờ bằng delay() (đơn vị ms) – đây chính là vTaskDelay()
    //   'C': chờ tới mốc tuyệt đối kế tiếp bằng vTaskDelayUntil(&last, ...) – đổi ms sang tick bằng pdMS_TO_TICKS
    (void)last;
  }
}

void taskLoad(void *arg) {                         // "Wi-Fi giả lập": ưu tiên cao hơn task lấy mẫu
  TickType_t last = xTaskGetTickCount();
  vTaskDelay(pdMS_TO_TICKS(50));
  last = xTaskGetTickCount();
  for (;;) {
    burn_us(TB_US);
    vTaskDelayUntil(&last, pdMS_TO_TICKS(PB_MS));
  }
}

void taskProcess(void *arg) {
  sample_t s;
  bool have_prev = false, done = false;
  uint32_t prev_t = 0, t_first = 0, sec_start = 0;
  stats_t st_all, st_1s;
  stats_reset(&st_all); stats_reset(&st_1s);
  for (;;) {
    g_hb_ms[1] = millis();
    if (g_hang) { for (;;) { g_sink = g_sink + 1; } }          // TN3: mô phỏng treo (vòng lặp vô hạn, không nhường CPU)
    uint32_t cnt = rb_count(&g_rb);
    if (cnt > g_rb_max) g_rb_max = cnt;
    while (rb_pop(&g_rb, &s)) {
      if (s.seq < N_SKIP || done) continue;
      if (!have_prev) {
        have_prev = true; prev_t = t_first = sec_start = s.t_us;
        g_tlog[g_nlog++] = s.t_us;
        continue;
      }
      uint32_t dt = s.t_us - prev_t;                            // hiệu không dấu: đúng cả khi micros() tràn
      prev_t = s.t_us;
      if (s.t_us - t_first >= 10000000UL) {                     // hết cửa sổ 10 s
        done = true;
        // độ lệch của nhãn cuối trong cửa sổ so với lưới t0 + k*T0 (tính trên ESP32 vì chỉ in N_IN nhãn đầu)
        int32_t lech = (int32_t)(g_tlog[g_nlog - 1] - t_first) - (int32_t)((g_nlog - 1) * T0_US);
        snap_t f = { 1, g_nlog, (float)stats_mean(&st_all), (float)stats_std(&st_all), (float)st_all.min,
                     (float)st_all.max, g_rb.overflow, g_rb_max, lech };
        xQueueSend(g_snapq, &f, 0);
        continue;
      }
      if (g_nlog < N_LOG) g_tlog[g_nlog++] = s.t_us;
      stats_add(&st_all, dt); stats_add(&st_1s, dt);
      if (s.t_us - sec_start >= 1000000UL) {                    // mỗi giây gửi một bản tóm tắt
        snap_t a = { 0, (uint32_t)st_1s.n, (float)stats_mean(&st_1s), (float)stats_std(&st_1s), (float)st_1s.min,
                     (float)st_1s.max, g_rb.overflow, g_rb_max, 0 };
        xQueueSend(g_snapq, &a, 0);
        stats_reset(&st_1s); sec_start = s.t_us;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void taskPrint(void *arg) {
  snap_t a;
  for (;;) {
    if (xQueueReceive(g_snapq, &a, portMAX_DELAY) != pdTRUE) continue;
    if (!a.final_) {
      Serial.printf("# 1s: n=%lu dt_tb=%.1f sd=%.1f min=%.0f max=%.0f us  overflow=%lu rb_max=%lu\n",
                    (unsigned long)a.n, a.mean, a.sd, a.mn, a.mx, (unsigned long)a.ovf, (unsigned long)a.rbmax);
      continue;
    }
    Serial.printf("# TONG KET 10 s: N_10s=%lu dt_tb=%.2f sd=%.2f min=%.0f max=%.0f us lech_cuoi_us=%ld overflow=%lu rb_max=%lu\n",
                  (unsigned long)a.n, a.mean, a.sd, a.mn, a.mx, (long)a.lech_us, (unsigned long)a.ovf, (unsigned long)a.rbmax);
    Serial.printf("# stack con trong (byte): sample=%u process=%u print=%u\n",
                  (unsigned)uxTaskGetStackHighWaterMark(hSample), (unsigned)uxTaskGetStackHighWaterMark(hProcess),
                  (unsigned)uxTaskGetStackHighWaterMark(NULL));
    Serial.println("k,t_us");
    for (uint32_t k = 0; k < g_nlog && k < N_IN; k++) Serial.printf("%lu,%lu\n", (unsigned long)k, (unsigned long)g_tlog[k]);
    Serial.println("# XONG");
  }
}

// ----------------------------- 4. THÍ NGHIỆM 3: watchdog phần mềm ---------------------
// Nút Wokwi CÓ mô phỏng dội phím (bounce): một lần nhấn sinh nhiều cạnh xuống. Ở đây dội phím VÔ HẠI vì ISR chỉ
// ghi nhận CẠNH ĐẦU TIÊN (cờ g_hang chặn các lần sau) – không cần đặt bounce="0" trong diagram.json.
void IRAM_ATTR isrButton() {
  if (!g_hang) { g_hang_ms = millis(); g_hang = true; }
}

void taskWatchdog(void *arg) {
  const char *names[2] = { "LayMau", "XuLy" };
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(WDT_CHECK_MS));
    uint32_t now = millis();
    // TODO(4): với mỗi task i = 0, 1: "tuổi" nhịp tim = now - g_hb_ms[i].
    //   Nếu tuổi > WDT_TIMEOUT_MS: in MỘT dòng chẩn đoán gồm tên task names[i], tuổi, g_hang_ms, now, độ trễ phát hiện
    //   (now - g_hang_ms), g_first_ovf_ms và g_rb.overflow; gọi Serial.flush() rồi ESP.restart().
    (void)names; (void)now;
    digitalWrite(PIN_LED, !digitalRead(PIN_LED));   // LED nhấp nháy = hệ thống khoẻ
  }
}

// ----------------------------- 5. THÍ NGHIỆM 2: đảo ngược độ ưu tiên -----------------
SemaphoreHandle_t g_lock;
TickType_t g_base;
volatile uint32_t tL0, tL_lock, tL_unlock, tH0, tH_req, tH_acq, tH_end, tM0, tM_end;
volatile bool fL = false, fH = false, fM = false;

void taskL(void *arg) {           // ưu tiên 2: giữ tài nguyên dùng chung (vd bus I2C) CL_MS
  TickType_t t = g_base;
  vTaskDelayUntil(&t, pdMS_TO_TICKS(100));
  tL0 = micros();
  // TODO(5b): lấy khoá g_lock (chờ vô hạn), ghi tL_lock = micros(), chạy đoạn găng burn_us(CL_MS * 1000),
  //           ghi tL_unlock = micros(), nhả khoá.
  fL = true; vTaskDelete(NULL);
}
void taskM(void *arg) {           // ưu tiên 3: không cần khoá, chạy CM_MS
  TickType_t t = g_base;
  vTaskDelayUntil(&t, pdMS_TO_TICKS(120));
  tM0 = micros();
  burn_us(CM_MS * 1000);
  tM_end = micros();
  fM = true; vTaskDelete(NULL);
}
void taskH(void *arg) {           // ưu tiên 4: chạy 5 ms, rồi cần khoá trong 15 ms
  TickType_t t = g_base;
  vTaskDelayUntil(&t, pdMS_TO_TICKS(110));
  tH0 = micros();
  burn_us(5000);
  // TODO(5c): ghi tH_req = micros(); lấy khoá; ghi tH_acq = micros(); chạy burn_us(15000); nhả khoá.
  tH_end = micros();
  fH = true; vTaskDelete(NULL);
}

void start_inversion() {
  // TODO(5a): tạo khoá theo KHOA: 0 -> semaphore nhị phân (nhớ "give" một lần để khoá ở trạng thái rảnh), 1 -> mutex
  g_lock = NULL;   // TODO(5a): thay bằng lời gọi tạo khoá phù hợp với KHOA
  g_base = xTaskGetTickCount();
  xTaskCreatePinnedToCore(taskL, "L", 3072, NULL, 2, NULL, CORE_SAMPLE);
  xTaskCreatePinnedToCore(taskM, "M", 3072, NULL, 3, NULL, CORE_SAMPLE);
  xTaskCreatePinnedToCore(taskH, "H", 3072, NULL, 4, NULL, CORE_SAMPLE);
}

void report_inversion() {
  auto ms = [](uint32_t t) { return (t - tL0) / 1000.0f; };
  Serial.printf("# KHOA=%s  CL=%lu ms  CM=%lu ms  (moc 0 = luc L bat dau)\n", KHOA ? "mutex" : "semaphore nhi phan",
                (unsigned long)CL_MS, (unsigned long)CM_MS);
  Serial.println("su_kien,t_ms");
  Serial.printf("L_bat_dau,%.2f\nL_lay_khoa,%.2f\nL_nha_khoa,%.2f\n", ms(tL0), ms(tL_lock), ms(tL_unlock));
  Serial.printf("H_bat_dau,%.2f\nH_xin_khoa,%.2f\nH_duoc_khoa,%.2f\nH_xong,%.2f\n", ms(tH0), ms(tH_req), ms(tH_acq), ms(tH_end));
  Serial.printf("M_bat_dau,%.2f\nM_xong,%.2f\n", ms(tM0), ms(tM_end));
  Serial.printf("# H bi chan = %.2f ms\n", (tH_acq - tH_req) / 1000.0f);
}

// ----------------------------- 6. setup / loop ------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(200);
  pinMode(PIN_PROBE, OUTPUT); pinMode(PIN_LED, OUTPUT); pinMode(PIN_BTN, INPUT_PULLUP);
  Serial.printf("# MSSV=%lu d=%lu c=%lu THI_NGHIEM=%d CHE_DO=%c TAI_NEN=%d KHOA=%d reset_reason=%d\n",
                (unsigned long)MSSV, (unsigned long)D, (unsigned long)C, THI_NGHIEM, CHE_DO, TAI_NEN, KHOA,
                (int)esp_reset_reason());
  Serial.printf("# T0_US=%lu TX_US=%lu TB_US=%lu PB_MS=%lu CL_MS=%lu CM_MS=%lu WDT_TIMEOUT_MS=%lu\n",
                (unsigned long)T0_US, (unsigned long)TX_US, (unsigned long)TB_US, (unsigned long)PB_MS,
                (unsigned long)CL_MS, (unsigned long)CM_MS, (unsigned long)WDT_TIMEOUT_MS);
  calibrate_burn();
  uint32_t t = micros(); burn_us(10000); t = micros() - t;
  Serial.printf("# burn_us(10000) do duoc %lu us (iter/us=%.2f)\n", (unsigned long)t, g_iter_per_us);

  if (THI_NGHIEM == 2) { start_inversion(); return; }

  rb_init(&g_rb);
  g_snapq = xQueueCreate(8, sizeof(snap_t));
  g_hb_ms[0] = g_hb_ms[1] = millis();
  xTaskCreatePinnedToCore(taskProcess, "XuLy", 4096, NULL, 3, &hProcess, CORE_OTHER);
  xTaskCreatePinnedToCore(taskPrint, "In", 4096, NULL, 1, &hPrint, CORE_OTHER);
  xTaskCreatePinnedToCore(taskSample, "LayMau", 3072, NULL, 5, &hSample, CORE_SAMPLE);
  if (THI_NGHIEM == 1 && TAI_NEN) xTaskCreatePinnedToCore(taskLoad, "TaiNen", 2048, NULL, 6, &hLoad, CORE_SAMPLE);
  if (THI_NGHIEM == 3) {
    attachInterrupt(digitalPinToInterrupt(PIN_BTN), isrButton, FALLING);
    xTaskCreatePinnedToCore(taskWatchdog, "WDT", 3072, NULL, 4, NULL, CORE_OTHER);
  }
}

void loop() {
  if (THI_NGHIEM == 2 && fL && fH && fM) {
    report_inversion();
    fL = false;                                    // chỉ in một lần
  }
  delay(100);
}

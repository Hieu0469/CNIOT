// =====================================================================================
// CE339 – Lab TH1: Chuỗi đo trên ESP32 – đọc cảm biến, đặc tính nhiễu, lọc số
// MÃ KHUNG cho sinh viên – tìm các chỗ "TODO"
// Phần cứng (Wokwi): ESP32 DevKit C v4 + MPU6050 (I2C: SDA=21, SCL=22) + module NTC (OUT -> GPIO34)
// Tệp đi kèm trong cùng project: filters.h/.c (SV viết), noise.h/.c (cho sẵn)
// Lưu ý Wokwi: ADC mô phỏng LÝ TƯỞNG (không nhiễu, không phi tuyến) và MPU6050 không nhiễu, không rung
//   -> mọi nhiễu trong bài được CỘNG BẰNG CODE (noise.c). Mọi nhịp lấy mẫu do ESP32 tự định thời bằng micros().
// =====================================================================================
#include <Arduino.h>
#include <Wire.h>
#include <math.h>
#include "filters.h"
#include "noise.h"

// ----------------------------- 1. THAM SỐ CÁ NHÂN (SV sửa MSSV) ---------------------
#define MSSV 23520447UL
#define CHE_DO 1        // 0: ADC thô | 1: + nhiễu Gauss | 2: + nhiễu Gauss + xung | 3: bậc thang không nhiễu
                        // 4: MPU6050 1 kHz vào RAM rồi in | 5: hiệu chuẩn 2 điểm

const uint32_t D = MSSV % 10;          // chữ số cuối
const uint32_t C = (MSSV / 10) % 10;   // chữ số áp cuối

const float    T_SET      = 20.0f + 3.0f * C;       // nhiệt độ đặt trên thanh trượt NTC (°C)
const float    SIGMA_N    = 4.0f + D;               // nhiễu Gauss mô phỏng (LSB)
const uint8_t  N_MA       = 5 + 2 * (D % 3);        // cửa sổ trung bình trượt
const float    ALPHA      = 0.1f * (1 + C % 3);     // hệ số EMA
const uint8_t  N_MED      = 5;                      // cửa sổ trung vị (hệ thống xuyên suốt)
const uint32_t K_SPIKE    = 50 + 10 * D;            // chu kỳ xung nhiễu (mẫu)
const int32_t  A_SPIKE    = 1000;                   // biên độ xung (LSB)
const int32_t  STEP_LSB   = 400;                    // biên độ bậc thang (LSB)
const uint32_t N0_STEP    = 500;                    // chỉ số mẫu bắt đầu bậc thang
const uint32_t I2C_HZ     = 100000UL * (1 + C % 4); // xung nhịp I2C
const float    GAIN_ERR   = 1.02f + 0.01f * D;      // "cảm biến lệch" mô phỏng: độ lợi
const float    OFFSET_ERR = 0.5f + 0.3f * C;        //                          offset (°C)

// Hệ số hiệu chuẩn – SV điền sau khi đo ở 0 °C và 60 °C (CHE_DO 5)
float CAL_G  = 1.0f;                   // TODO T3a: g  = 60 / (x_H - x_L)
float CAL_X0 = 0.0f;                   // TODO T3a: x0 = x_L

// ----------------------------- 2. HẰNG SỐ PHẦN CỨNG ---------------------------------
#define PIN_NTC   34
#define PIN_SDA   21
#define PIN_SCL   22
#define MPU_ADDR  0x68
const uint32_t FS_HZ = 100;            // tần số lấy mẫu ADC trong thí nghiệm nhiễu
const uint32_t N_SAMPLES = 1000;
const float VCC = 3.3f, R_FIXED = 10000.0f, R0 = 10000.0f, BETA = 3950.0f, T0_K = 298.15f;
// Module wokwi-ntc-temperature-sensor: 3V3 -- R 10k (cố định) -- OUT -- NTC 10k -- GND
//   (NTC ở NỬA DƯỚI mạch chia áp, theo tài liệu Wokwi) => V_out = Vcc*R_ntc/(R_ntc + R): mã ADC GIẢM khi nhiệt độ TĂNG.
const float ACC_LSB_PER_G = 1.0f;      // TODO T2: độ nhạy (LSB/g) ở dải ±4 g – tra bảng trong slide "Đọc datasheet"

rng_t rng;
ma_t  f_ma;  ema_t f_ema;  med_t f_med;

// ----------------------------- 3. NTC: mã ADC -> °C  (TODO T1) ----------------------
float ntc_code_to_celsius(float code) {
  // TODO T1: đổi mã ADC (0..4095) -> °C.
  //  (a) x = code / 4095 là tỉ số V_out/V_cc; giới hạn x trong (0,001; 0,999) để tránh chia 0.
  //  (b) Tìm R_ntc từ mạch chia áp của module Wokwi (NTC ở nửa DƯỚI, nối GND; điện trở cố định R_FIXED nối 3V3):
  //      tự biến đổi x = R_ntc/(R_ntc + R_FIXED) để rút ra R_ntc theo x.
  //  (c) Phương trình Beta: 1/T = 1/T0 + ln(R_ntc/R0)/B  (T, T0 tính bằng KELVIN).
  (void)code;
  return 0.0f;
}

// ----------------------------- 4. MPU6050 qua I2C (cho sẵn) -------------------------
void i2c_write_reg(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(MPU_ADDR); Wire.write(reg); Wire.write(val); Wire.endTransmission();
}
bool mpu_read_accel(int16_t *ax, int16_t *ay, int16_t *az) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3B);                                   // ACCEL_XOUT_H
  if (Wire.endTransmission(false) != 0) return false; // repeated START
  if (Wire.requestFrom((uint8_t)MPU_ADDR, (uint8_t)6) != 6) return false;
  uint8_t b[6];
  for (int i = 0; i < 6; i++) b[i] = Wire.read();     // đọc tuần tự – KHÔNG viết (read()<<8)|read()
  *ax = (int16_t)((b[0] << 8) | b[1]);
  *ay = (int16_t)((b[2] << 8) | b[3]);
  *az = (int16_t)((b[4] << 8) | b[5]);
  return true;
}
void i2c_scan() {
  Serial.print("# I2C scan:");
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) { Serial.print(" 0x"); Serial.print(a, HEX); }
  }
  Serial.println();
}
void mpu_init() {
  i2c_write_reg(0x6B, 0x00);   // PWR_MGMT_1: đánh thức
  // Hai thanh ghi dưới đây chỉ có ý nghĩa trên CHIP THẬT (DLPF 260 Hz, SMPLRT_DIV = 0).
  // Wokwi KHÔNG mô phỏng DLPF, FIFO, SMPLRT_DIV (ghi không lỗi nhưng KHÔNG có tác dụng) -> nhịp 1 kHz do ESP32 tự định thời.
  i2c_write_reg(0x1A, 0x00);   // CONFIG: DLPF_CFG = 0 (chip thật: băng thông gia tốc 260 Hz) – không tác dụng trên Wokwi
  i2c_write_reg(0x19, 0x00);   // SMPLRT_DIV = 0 – không tác dụng trên Wokwi
  i2c_write_reg(0x1C, 0x08);   // ACCEL_CONFIG: AFS_SEL = 1 -> ±4 g
}

// ----------------------------- 5. In thông số -----------------------------------------
void print_params() {
  Serial.printf("# MSSV=%lu d=%lu c=%lu CHE_DO=%d\n", (unsigned long)MSSV, (unsigned long)D, (unsigned long)C, CHE_DO);
  Serial.printf("# T_SET=%.1f SIGMA_N=%.0f N_MA=%u ALPHA=%.2f N_MED=%u K_SPIKE=%lu A_SPIKE=%ld STEP=%ld N0=%lu I2C_HZ=%lu\n",
                T_SET, SIGMA_N, N_MA, ALPHA, N_MED, (unsigned long)K_SPIKE, (long)A_SPIKE, (long)STEP_LSB,
                (unsigned long)N0_STEP, (unsigned long)I2C_HZ);
}

// ----------------------------- 6. Thí nghiệm nhiễu & lọc (CHE_DO 0..3) ---------------
void run_noise_experiment() {
  stats_t s_x, s_ma, s_ema, s_med;
  stats_reset(&s_x); stats_reset(&s_ma); stats_reset(&s_ema); stats_reset(&s_med);
  ma_init(&f_ma, N_MA); ema_init(&f_ema, ALPHA); med_init(&f_med, N_MED);
  rng_seed(&rng, (uint32_t)MSSV);

  Serial.println("n,x,ma,ema,med");
  uint32_t t_next = micros();
  const uint32_t TS_US = 1000000UL / FS_HZ;
  for (uint32_t n = 0; n < N_SAMPLES; n++) {
    while ((int32_t)(micros() - t_next) < 0) { }     // chờ theo MỐC TUYỆT ĐỐI (xem Lab TH2)
    t_next += TS_US;
    int32_t x = analogRead(PIN_NTC);
    if (CHE_DO == 1 || CHE_DO == 2) x += noise_gauss_lsb(&rng, SIGMA_N);
    if (CHE_DO == 2) x += noise_spike(n, K_SPIKE, A_SPIKE);
    if (CHE_DO == 3 && n >= N0_STEP) x += STEP_LSB;
    if (x < 0) x = 0;
    if (x > 4095) x = 4095;

    float   y_ma  = ma_update(&f_ma, x);
    float   y_ema = ema_update(&f_ema, (float)x);
    int32_t y_med = med_update(&f_med, x);
    Serial.printf("%lu,%ld,%.2f,%.2f,%ld\n", (unsigned long)n, (long)x, y_ma, y_ema, (long)y_med);

    if (n >= 50) {                                    // bỏ 50 mẫu đầu (khởi động bộ lọc)
      stats_add(&s_x, x); stats_add(&s_ma, y_ma); stats_add(&s_ema, y_ema); stats_add(&s_med, y_med);
    }
  }
  double sx = stats_std(&s_x);
  Serial.printf("# mean_x=%.2f  sigma: x=%.3f ma=%.3f ema=%.3f med=%.3f (LSB, n>=50)\n",
                stats_mean(&s_x), sx, stats_std(&s_ma), stats_std(&s_ema), stats_std(&s_med));
  Serial.printf("# ENOB=%.2f bit   T(mean_x)=%.2f C\n", enob_from_sigma(sx, 12), ntc_code_to_celsius(stats_mean(&s_x)));
}

// ----------------------------- 7. MPU6050 1 kHz vào RAM (CHE_DO 4) --------------------
// Nhịp 1 kHz do vòng chờ micros() tạo ra (Wokwi không có ngắt data-ready / SMPLRT_DIV).
// Trên Wokwi MPU6050 trả về đúng giá trị thanh trượt, KHÔNG nhiễu -> 1000 dòng gần như giống hệt nhau;
// mục đích của chế độ này là đo THỜI GIAN (capture, in) và BỘ NHỚ, không phải phân tích rung.
int16_t acc_buf[1000][3];                              // 1000 x 3 x 2 = 6000 byte
void run_mpu_capture() {
  uint32_t t0 = micros(), t_next = t0;
  for (int n = 0; n < 1000; n++) {
    while ((int32_t)(micros() - t_next) < 0) { }
    t_next += 1000;
    mpu_read_accel(&acc_buf[n][0], &acc_buf[n][1], &acc_buf[n][2]);
  }
  uint32_t t_cap = micros() - t0;
  uint32_t t1 = micros();
  Serial.println("n,ax,ay,az");
  for (int n = 0; n < 1000; n++) Serial.printf("%d,%d,%d,%d\n", n, acc_buf[n][0], acc_buf[n][1], acc_buf[n][2]);
  Serial.flush();                                      // chờ UART gửi hết
  uint32_t t_print = micros() - t1;
  Serial.printf("# capture_us=%lu print_us=%lu\n", (unsigned long)t_cap, (unsigned long)t_print);
}

// ----------------------------- 8. Hiệu chuẩn 2 điểm (CHE_DO 5) -------------------------
void run_calibration_loop() {
  rng_seed(&rng, (uint32_t)MSSV);
  for (;;) {
    stats_t s; stats_reset(&s);
    for (int i = 0; i < 200; i++) {                    // 200 mẫu ở 100 Hz = 2 s
      int32_t x = analogRead(PIN_NTC) + noise_gauss_lsb(&rng, SIGMA_N);
      stats_add(&s, x);
      delay(10);
    }
    float t_true = ntc_code_to_celsius(stats_mean(&s));
    float x_read = GAIN_ERR * t_true + OFFSET_ERR;     // cảm biến lệch mô phỏng
    float t_cal  = x_read;                            // TODO T3b: áp dụng hiệu chuẩn T = g*(x - x0)
    Serial.printf("x_doc=%.2f  T_hieuchuan=%.2f  (T_NTC=%.2f)\n", x_read, t_cal, t_true);
  }
}

// ----------------------------- 9. setup / loop ------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(200);
  analogReadResolution(12);
  Wire.begin(PIN_SDA, PIN_SCL);
  Wire.setClock(I2C_HZ);
  print_params();
  i2c_scan();
  mpu_init();

  int16_t ax, ay, az;
  uint32_t t0 = micros();
  for (int i = 0; i < 100; i++) mpu_read_accel(&ax, &ay, &az);
  uint32_t t_read = (micros() - t0) / 100;
  Serial.printf("# I2C doc 6 byte: %lu us/lan @ %lu Hz\n", (unsigned long)t_read, (unsigned long)I2C_HZ);
  Serial.printf("# ax=%d ay=%d az=%d LSB -> az=%.3f g  |a|=%.3f g\n", ax, ay, az, az / ACC_LSB_PER_G,
                sqrtf((float)ax * ax + (float)ay * ay + (float)az * az) / ACC_LSB_PER_G);

  if (CHE_DO <= 3) run_noise_experiment();
  else if (CHE_DO == 4) run_mpu_capture();
  else run_calibration_loop();
  Serial.println("# XONG");
}

void loop() { delay(1000); }

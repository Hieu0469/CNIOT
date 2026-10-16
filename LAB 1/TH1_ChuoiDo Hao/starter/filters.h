/* filters.h – CE339 Lab TH1: bộ lọc số và thống kê chạy trên MCU (C thuần)
 * Dùng chung cho: sketch.ino (ESP32/Wokwi) và test_filters.c (biên dịch trên PC bằng gcc).
 * Liên hệ Chương 3, Phần 5 "Xử lý số trên MCU": Lọc trung bình trượt, Lọc EMA, Lọc trung vị.
 */
#ifndef FILTERS_H
#define FILTERS_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MA_NMAX  16   /* cửa sổ trung bình trượt tối đa */
#define MED_NMAX 9    /* cửa sổ trung vị tối đa (số lẻ) */

/* ---- Trung bình trượt (Moving Average), tổng chạy O(1) ---- */
typedef struct {
    int32_t buf[MA_NMAX];  /* bộ đệm vòng chứa N mẫu gần nhất */
    int32_t sum;           /* tổng chạy (int32: không tích luỹ sai số làm tròn) */
    uint8_t N;             /* độ dài cửa sổ */
    uint8_t idx;           /* vị trí sẽ ghi mẫu kế tiếp */
    uint8_t count;         /* số mẫu đã nhận (bão hoà ở N) */
} ma_t;
void  ma_init(ma_t *f, uint8_t N);
float ma_update(ma_t *f, int32_t x);   /* trả về trung bình của min(count, N) mẫu gần nhất */

/* ---- EMA: y[n] = alpha*x[n] + (1-alpha)*y[n-1] ---- */
typedef struct {
    float y;
    float alpha;
    uint8_t started;       /* 0: chưa có mẫu nào -> mẫu đầu tiên gán thẳng y = x */
} ema_t;
void  ema_init(ema_t *f, float alpha);
float ema_update(ema_t *f, float x);

/* ---- Trung vị cửa sổ N (lẻ) ---- */
typedef struct {
    int32_t buf[MED_NMAX];
    uint8_t N, idx, count;
} med_t;
void    med_init(med_t *f, uint8_t N);
int32_t med_update(med_t *f, int32_t x);  /* trung vị của min(count, N) mẫu gần nhất */

/* ---- Thống kê chạy (Welford): trung bình, độ lệch chuẩn, min, max ---- */
typedef struct {
    uint32_t n;
    double mean, m2, min, max;
} stats_t;
void   stats_reset(stats_t *s);
void   stats_add(stats_t *s, double x);
double stats_mean(const stats_t *s);
double stats_std(const stats_t *s);     /* độ lệch chuẩn mẫu (chia n-1); n < 2 -> 0 */

/* ENOB ước lượng từ độ lệch chuẩn nhiễu (đơn vị LSB) của ADC N bit: ENOB = N - log2(sigma*sqrt(12)) */
double enob_from_sigma(double sigma_lsb, int nbits);

#ifdef __cplusplus
}
#endif
#endif

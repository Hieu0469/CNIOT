/* filters.c – CE339 Lab TH1: SV TỰ VIẾT các hàm có đánh dấu TODO.
 * Kiểm thử trên PC trước:  gcc -O2 -Wall -o test_filters test_filters.c filters.c noise.c -lm
 *                          ./test_filters du_lieu_mau.csv <N_MA> <ALPHA>
 * Khi mọi kiểm tra "OK" mới chép sang Wokwi (thêm tệp filters.h, filters.c vào project).
 * Ghi rõ trong Nhật ký AI nếu có dùng AI sinh mã; bạn phải giải thích được từng dòng khi vấn đáp.
 */
#include "filters.h"
#include <math.h>

/* ------------------------------------------------ Trung bình trượt */
void ma_init(ma_t *f, uint8_t N) {
    if (N < 1) N = 1;
    if (N > MA_NMAX) N = MA_NMAX;
    f->N = N; f->idx = 0; f->count = 0; f->sum = 0;
    for (int i = 0; i < MA_NMAX; i++) f->buf[i] = 0;
}

float ma_update(ma_t *f, int32_t x) {
    /* (1): Cài đặt O(1) bằng TỔNG CHẠY */
    f->sum -= f->buf[f->idx];     // Trừ mẫu cũ nhất
    f->sum += x;                  // Cộng mẫu mới
    f->buf[f->idx] = x;           // Ghi x vào bộ đệm vòng
    
    f->idx = (f->idx + 1) % f->N; // Tăng chỉ số theo modulo N
    
    if (f->count < f->N) {
        f->count++;               // count tăng tới N rồi dừng
    }
    
    return (float)f->sum / f->count; // Trả về trung bình
}

/* ------------------------------------------------ EMA */
void ema_init(ema_t *f, float alpha) {
    f->alpha = alpha; f->y = 0.0f; f->started = 0;
}

float ema_update(ema_t *f, float x) {
    /* (2): y[n] = alpha*x[n] + (1 - alpha)*y[n-1] */
    if (f->started == 0) {
        f->y = x;                 // Mẫu đầu tiên gán thẳng y = x
        f->started = 1;
    } else {
        f->y = f->alpha * x + (1.0f - f->alpha) * f->y;
    }
    return f->y;
}

/* ------------------------------------------------ Trung vị */
void med_init(med_t *f, uint8_t N) {
    if (N < 1) N = 1;
    if (N > MED_NMAX) N = MED_NMAX;
    if ((N & 1u) == 0) N--;
    f->N = N; f->idx = 0; f->count = 0;
    for (int i = 0; i < MED_NMAX; i++) f->buf[i] = 0;
}

int32_t med_update(med_t *f, int32_t x) {
    /* (3): Ghi x vào bộ đệm vòng và tìm trung vị */
    f->buf[f->idx] = x;
    f->idx = (f->idx + 1) % f->N;
    if (f->count < f->N) f->count++;

    // Chép mảng ra mảng tạm (không sort mảng gốc làm hỏng thứ tự đệm vòng)
    int32_t temp[MED_NMAX];
    for (uint8_t i = 0; i < f->count; i++) {
        temp[i] = f->buf[i];
    }

    // Sắp xếp chèn (Insertion sort)
    for (uint8_t i = 1; i < f->count; i++) {
        int32_t key = temp[i];
        int j = i - 1;
        while (j >= 0 && temp[j] > key) {
            temp[j + 1] = temp[j];
            j = j - 1;
        }
        temp[j + 1] = key;
    }

    // Trả về phần tử ở giữa
    return temp[f->count / 2];
}

/* ------------------------------------------------ Thống kê chạy (Welford) */
void stats_reset(stats_t *s) { 
    s->n = 0; s->mean = 0; s->m2 = 0; s->min = 0; s->max = 0; 
}

void stats_add(stats_t *s, double x) {
    /* (4): Thuật toán Welford */
    if (s->n == 0) {
        s->min = x;
        s->max = x;
    } else {
        if (x < s->min) s->min = x;
        if (x > s->max) s->max = x;
    }
    
    s->n++;
    double delta = x - s->mean;
    s->mean += delta / s->n;
    s->m2 += delta * (x - s->mean);
}

double stats_mean(const stats_t *s) { 
    return s->mean; 
}

double stats_std(const stats_t *s) {
    /* (5): Độ lệch chuẩn mẫu */
    if (s->n < 2) return 0.0;
    return sqrt(s->m2 / (s->n - 1));
}

double enob_from_sigma(double sigma_lsb, int nbits) {
    /* (6): Tính ENOB */
    if (sigma_lsb <= 0.0) return (double)nbits;
    return (double)nbits - log2(sigma_lsb * sqrt(12.0));
}
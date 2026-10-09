/* filters.c – CE339 Lab TH1: SV TỰ VIẾT các hàm có đánh dấu TODO.
 * Kiểm thử trên PC trước:  gcc -O2 -Wall -o test_filters test_filters.c filters.c noise.c -lm
 *                          ./test_filters du_lieu_mau.csv <N_MA> <ALPHA>
 * Khi mọi kiểm tra "OK" mới chép sang Wokwi (thêm tệp filters.h, filters.c vào project).
 * Ghi rõ trong Nhật ký AI nếu có dùng AI sinh mã; bạn phải giải thích được từng dòng khi vấn đáp.
 */
#include "filters.h"
#include <math.h>

/* ------------------------------------------------ Trung bình trượt (slide "Lọc trung bình trượt") */
void ma_init(ma_t *f, uint8_t N) {
    if (N < 1) N = 1;
    if (N > MA_NMAX) N = MA_NMAX;
    f->N = N; f->idx = 0; f->count = 0; f->sum = 0;
    for (int i = 0; i < MA_NMAX; i++) f->buf[i] = 0;
}

float ma_update(ma_t *f, int32_t x) {
    /* TODO(1): cài đặt O(1) bằng TỔNG CHẠY – KHÔNG cộng lại N phần tử mỗi lần.
       - cập nhật f->sum (cộng mẫu mới, trừ mẫu cũ nhất đang ở f->buf[f->idx]);
       - ghi x vào bộ đệm vòng, tăng f->idx theo modulo f->N;
       - f->count tăng tới N rồi dừng;
       - trả về trung bình của f->count mẫu hiện có (lúc khởi động count < N). */
    (void)f; (void)x;
    return 0.0f;
}

/* ------------------------------------------------ EMA (slide "Lọc EMA: một dòng code, nhớ một biến") */
void ema_init(ema_t *f, float alpha) {
    f->alpha = alpha; f->y = 0.0f; f->started = 0;
}

float ema_update(ema_t *f, float x) {
    /* TODO(2): y[n] = alpha*x[n] + (1 - alpha)*y[n-1]; mẫu ĐẦU TIÊN gán thẳng y = x (dùng f->started). */
    (void)f; (void)x;
    return 0.0f;
}

/* ------------------------------------------------ Trung vị (slide "Lọc trung vị: vũ khí chống xung nhiễu") */
void med_init(med_t *f, uint8_t N) {
    if (N < 1) N = 1;
    if (N > MED_NMAX) N = MED_NMAX;
    if ((N & 1u) == 0) N--;
    f->N = N; f->idx = 0; f->count = 0;
    for (int i = 0; i < MED_NMAX; i++) f->buf[i] = 0;
}

int32_t med_update(med_t *f, int32_t x) {
    /* TODO(3): ghi x vào bộ đệm vòng (giống MA), rồi
       - chép f->count phần tử đang có ra mảng tạm (KHÔNG sắp xếp trực tiếp f->buf – vì sao?);
       - sắp xếp mảng tạm (sắp xếp chèn là đủ với N <= 9);
       - trả về phần tử ở giữa (chỉ số count/2). */
    (void)f; (void)x;
    return 0;
}

/* ------------------------------------------------ Thống kê chạy (Welford) */
void stats_reset(stats_t *s) { s->n = 0; s->mean = 0; s->m2 = 0; s->min = 0; s->max = 0; }

void stats_add(stats_t *s, double x) {
    /* TODO(4): thuật toán Welford – cập nhật n, mean, m2, min, max với MỘT mẫu mới, không lưu mảng.
       Gợi ý: delta = x - mean; mean += delta/n; m2 += delta*(x - mean_mới). */
    (void)s; (void)x;
}

double stats_mean(const stats_t *s) { return s->mean; }

double stats_std(const stats_t *s) {
    /* TODO(5): độ lệch chuẩn MẪU (chia n-1); n < 2 thì trả về 0. */
    (void)s;
    return 0.0;
}

double enob_from_sigma(double sigma_lsb, int nbits) {
    /* TODO(6): ENOB = N - log2(sigma * sqrt(12)); sigma <= 0 thì trả về nbits. */
    (void)sigma_lsb;
    return (double)nbits;
}

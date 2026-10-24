/* stats.h – CE339 Lab TH2: thống kê chạy (Welford) – cùng thuật toán bạn đã viết ở Lab TH1 */
#ifndef STATS_H
#define STATS_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { uint32_t n; double mean, m2, min, max; } stats_t;
void   stats_reset(stats_t *s);
void   stats_add(stats_t *s, double x);
double stats_mean(const stats_t *s);
double stats_std(const stats_t *s);      /* độ lệch chuẩn mẫu (chia n-1) */
#ifdef __cplusplus
}
#endif
#endif

/* stats.c – CE339 Lab TH2: thống kê chạy Welford.
 * TODO(3): CHÉP LỜI GIẢI CỦA BẠN từ Lab TH1 (hàm stats_add, stats_std trong filters.c) vào đây.
 */
#include "stats.h"
#include <math.h>
void stats_reset(stats_t *s) { s->n = 0; s->mean = 0; s->m2 = 0; s->min = 0; s->max = 0; }
void stats_add(stats_t *s, double x) { (void)s; (void)x; /* TODO(3) */ }
double stats_mean(const stats_t *s) { return s->mean; }
double stats_std(const stats_t *s) { (void)s; return 0.0; /* TODO(3) */ }

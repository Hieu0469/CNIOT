/* test_filters.c – CE339 Lab TH1: kiểm thử bộ lọc trên PC TRƯỚC khi nạp vào ESP32
 * Biên dịch:  gcc -O2 -Wall -o test_filters test_filters.c filters.c noise.c -lm
 * Chạy:       ./test_filters du_lieu_mau.csv 9 0.2
 *             (tham số: tệp CSV, N của trung bình trượt, alpha của EMA)
 * Tệp này đã viết sẵn – SV KHÔNG cần sửa. Mọi dòng "FAIL" nghĩa là filters.c chưa đúng.
 */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "filters.h"
#include "noise.h"

static int n_fail = 0;
static void check(const char *name, double got, double want, double tol) {
    int ok = fabs(got - want) <= tol;
    if (!ok) n_fail++;
    printf("  [%s] %-38s got=%10.4f  want=%10.4f\n", ok ? " OK " : "FAIL", name, got, want);
}

int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "du_lieu_mau.csv";
    int N = argc > 2 ? atoi(argv[2]) : 9;
    double alpha = argc > 3 ? atof(argv[3]) : 0.2;

    /* 1. Ví dụ tính tay trong slide "Lọc trung vị: vũ khí chống xung nhiễu" (nhân 10 để thành số nguyên) */
    printf("1) Vi du slide: x = {201, 203, 850, 202, 204}, cua so 3\n");
    int32_t xs[5] = {201, 203, 850, 202, 204};
    ma_t m; med_t md; ema_t e;
    ma_init(&m, 3); med_init(&md, 3); ema_init(&e, 0.5f);
    float yma[5]; int32_t ymed[5]; float yema[5];
    for (int i = 0; i < 5; i++) { yma[i] = ma_update(&m, xs[i]); ymed[i] = med_update(&md, xs[i]); yema[i] = ema_update(&e, (float)xs[i]); }
    check("MA3 tai n=2", yma[2], 418.0, 0.01);
    check("MA3 tai n=3", yma[3], 418.333, 0.01);
    check("MA3 tai n=4", yma[4], 418.667, 0.01);
    check("MED3 tai n=2", ymed[2], 203, 0);
    check("MED3 tai n=3", ymed[3], 203, 0);
    check("MED3 tai n=4", ymed[4], 204, 0);
    check("MA3 khoi dong n=0 (1 mau)", yma[0], 201, 0.01);
    check("EMA 0.5 tai n=2 (y0 = x0)", yema[2], 526.0, 0.01);

    /* 2. Thống kê Welford so với công thức hai lượt */
    printf("2) Welford\n");
    stats_t s; stats_reset(&s);
    double v[6] = {2, 4, 4, 4, 5, 5};
    for (int i = 0; i < 6; i++) stats_add(&s, v[i]);
    check("mean {2,4,4,4,5,5}", stats_mean(&s), 4.0, 1e-9);
    check("std mau (n-1)", stats_std(&s), sqrt(6.0 / 5.0), 1e-9);
    check("ENOB khi sigma = 3 LSB (slide ENOB)", enob_from_sigma(3.0, 12), 12 - log2(3 * sqrt(12.0)), 1e-9);

    /* 3. Dữ liệu mẫu: nhiễu Gauss (cột 2) và Gauss + xung (cột 3) */
    FILE *f = fopen(path, "r");
    if (!f) { printf("Khong mo duoc %s\n", path); return 2; }
    static int32_t xg[5000], xp[5000];
    int n = 0; char line[128];
    if (!fgets(line, sizeof line, f)) { fclose(f); return 2; }          /* bỏ dòng tiêu đề */
    while (n < 5000 && fgets(line, sizeof line, f)) {
        int k; long a, b;
        if (sscanf(line, "%d,%ld,%ld", &k, &a, &b) == 3) { xg[n] = (int32_t)a; xp[n] = (int32_t)b; n++; }
    }
    fclose(f);
    printf("3) Du lieu mau: %d mau, N_MA=%d, alpha=%.2f, N_MED=5\n", n, N, alpha);
    stats_t sx, sma, sema, smed; stats_reset(&sx); stats_reset(&sma); stats_reset(&sema); stats_reset(&smed);
    ma_init(&m, (uint8_t)N); ema_init(&e, (float)alpha); med_init(&md, 5);
    for (int i = 0; i < n; i++) {
        float a = ma_update(&m, xg[i]); float b = ema_update(&e, (float)xg[i]); int32_t c = med_update(&md, xg[i]);
        if (i >= 50) { stats_add(&sx, xg[i]); stats_add(&sma, a); stats_add(&sema, b); stats_add(&smed, c); }
    }
    double s0 = stats_std(&sx);
    printf("   sigma_x = %.3f LSB (mean %.2f), ENOB = %.2f bit\n", s0, stats_mean(&sx), enob_from_sigma(s0, 12));
    printf("   %-8s %10s %12s %8s\n", "bo loc", "sigma do", "ly thuyet", "lech %");
    double th_ma = s0 / sqrt((double)N), th_ema = s0 * sqrt(alpha / (2 - alpha)), th_med = s0 * 1.2533 / sqrt(5.0);
    printf("   %-8s %10.3f %12.3f %7.1f%%\n", "MA", stats_std(&sma), th_ma, 100 * (stats_std(&sma) / th_ma - 1));
    printf("   %-8s %10.3f %12.3f %7.1f%%\n", "EMA", stats_std(&sema), th_ema, 100 * (stats_std(&sema) / th_ema - 1));
    printf("   %-8s %10.3f %12.3f %7.1f%%  (xap xi 1,2533/sqrt(N))\n", "Trung vi", stats_std(&smed), th_med, 100 * (stats_std(&smed) / th_med - 1));
    if (fabs(stats_std(&sma) / th_ma - 1) > 0.2) { n_fail++; printf("  [FAIL] sigma MA lech > 20%% so voi ly thuyet\n"); }
    if (fabs(stats_std(&sema) / th_ema - 1) > 0.2) { n_fail++; printf("  [FAIL] sigma EMA lech > 20%% so voi ly thuyet\n"); }

    /* 4. Xung nhiễu: đếm mẫu đầu ra lệch > 5*sigma_x khỏi trung bình */
    ma_init(&m, (uint8_t)N); ema_init(&e, (float)alpha); med_init(&md, 5);
    int cx = 0, cma = 0, cema = 0, cmed = 0; double mu = stats_mean(&sx), thr = 5 * s0;
    for (int i = 0; i < n; i++) {
        float a = ma_update(&m, xp[i]); float b = ema_update(&e, (float)xp[i]); int32_t c = med_update(&md, xp[i]);
        if (i >= 50) { cx += fabs(xp[i] - mu) > thr; cma += fabs(a - mu) > thr; cema += fabs(b - mu) > thr; cmed += fabs(c - mu) > thr; }
    }
    printf("4) Xung nhieu – so mau lech > 5 sigma: tho=%d  MA=%d  EMA=%d  trung vi=%d\n", cx, cma, cema, cmed);

    /* 5. Bậc thang không nhiễu: độ trễ d50 = số mẫu từ n0 tới khi đầu ra >= nền + S/2 */
    ma_init(&m, (uint8_t)N); ema_init(&e, (float)alpha); med_init(&md, 5);
    int d_ma = -1, d_ema = -1, d_med = -1; const int n0 = 500; const int32_t base = 2000, S = 400;
    for (int i = 0; i < 1000; i++) {
        int32_t x = base + (i >= n0 ? S : 0);
        float a = ma_update(&m, x); float b = ema_update(&e, (float)x); int32_t c = med_update(&md, x);
        if (i >= n0) {
            if (d_ma < 0 && a >= base + S / 2.0) d_ma = i - n0;
            if (d_ema < 0 && b >= base + S / 2.0) d_ema = i - n0;
            if (d_med < 0 && c >= base + S / 2.0) d_med = i - n0;
        }
    }
    int want_ema = (int)ceil(log(0.5) / log(1 - alpha)) - 1;
    printf("5) Bac thang (S = 400 LSB tai n0 = 500)\n");
    check("d50 MA  = (N-1)/2 (N le)", d_ma, (N - 1) / 2, 0);
    check("d50 EMA = ceil(ln0.5/ln(1-a)) - 1", d_ema, want_ema, 0);
    check("d50 trung vi 5 = 2", d_med, 2, 0);

    printf(n_fail ? "\n==> %d kiem tra FAIL\n" : "\n==> Tat ca kiem tra OK\n", n_fail);
    return n_fail ? 1 : 0;
}

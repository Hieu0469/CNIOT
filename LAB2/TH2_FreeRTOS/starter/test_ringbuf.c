/* test_ringbuf.c – CE339 Lab TH2: kiểm thử bộ đệm vòng trên PC TRƯỚC khi nạp vào ESP32
 * Biên dịch:  gcc -O2 -Wall -pthread -o test_ringbuf test_ringbuf.c ringbuf.c stats.c -lm
 * Chạy:       ./test_ringbuf
 * Tệp này đã viết sẵn – SV KHÔNG cần sửa. Mọi dòng "FAIL" nghĩa là ringbuf.c (hoặc stats.c) chưa đúng.
 */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <pthread.h>
#include "ringbuf.h"
#include "stats.h"

static int n_fail = 0;
#define CHECK(name, cond) do { int ok_ = (cond); if (!ok_) n_fail++; printf("  [%s] %s\n", ok_ ? " OK " : "FAIL", name); } while (0)

static ringbuf_t rb;                      /* 8 KiB + 12 byte: để ở vùng tĩnh như trên ESP32 */

/* ---- kiểm thử đa luồng: 1 luồng ghi, 1 luồng đọc (mô phỏng 2 task trên 2 core) ---- */
#define N_STRESS 2000000u
static volatile int stress_err = 0;
static void *producer(void *arg) {
    (void)arg;
    for (uint32_t i = 0; i < N_STRESS; ) {
        sample_t s = { i, i * 3u };
        if (rb_push(&rb, s)) i++;          /* đầy thì thử lại (không bỏ mẫu trong bài kiểm thử này) */
    }
    return NULL;
}
static void *consumer(void *arg) {
    (void)arg;
    sample_t s; uint32_t expect = 0;
    while (expect < N_STRESS) {
        if (rb_pop(&rb, &s)) {
            if (s.seq != expect || s.t_us != expect * 3u) { stress_err = 1; break; }
            expect++;
        }
    }
    return NULL;
}

int main(void) {
    sample_t s, o;
    printf("1) Co ban\n");
    rb_init(&rb);
    CHECK("rong: count = 0", rb_count(&rb) == 0);
    CHECK("rong: pop tra ve false", !rb_pop(&rb, &o));
    int all = 1;
    for (uint32_t i = 0; i < RB_CAP; i++) { s.seq = i; s.t_us = 1000u * i; all &= rb_push(&rb, s); }
    CHECK("ghi du RB_CAP phan tu deu thanh cong", all);
    CHECK("day: count = RB_CAP", rb_count(&rb) == RB_CAP);
    s.seq = 9999;
    CHECK("day: push tra ve false", !rb_push(&rb, s));
    CHECK("day: overflow = 1", rb.overflow == 1);
    all = 1;
    for (uint32_t i = 0; i < RB_CAP; i++) all &= rb_pop(&rb, &o) && o.seq == i && o.t_us == 1000u * i;
    CHECK("doc ra dung thu tu FIFO", all);
    CHECK("doc het: count = 0, pop false", rb_count(&rb) == 0 && !rb_pop(&rb, &o));

    printf("2) Vong qua bien (wrap-around) va tran so uint32\n");
    rb_init(&rb);
    rb.head = rb.tail = 0xFFFFFF00u;       /* sắp tràn số */
    uint32_t w = 0, r = 0; all = 1;
    for (int k = 0; k < 5000; k++) {        /* ghi 3, đọc 2 xen kẽ, rồi đọc hết */
        for (int j = 0; j < 3 && rb_count(&rb) < RB_CAP; j++) { s.seq = w++; s.t_us = 0; all &= rb_push(&rb, s); }
        for (int j = 0; j < 2; j++) if (rb_pop(&rb, &o)) { all &= (o.seq == r); r++; }
    }
    while (rb_pop(&rb, &o)) { all &= (o.seq == r); r++; }
    CHECK("thu tu dung sau khi head/tail tran so", all && r == w);

    printf("3) Da luong (2 luong ~ 2 core), %u mau\n", N_STRESS);
    if (n_fail) { printf("  [BO QUA] sua loi o phan 1-2 truoc (neu khong, luong ghi se lap vo han)\n"); n_fail++; }
    else {
        rb_init(&rb);
        pthread_t a, b;
        pthread_create(&b, NULL, consumer, NULL);
        pthread_create(&a, NULL, producer, NULL);
        pthread_join(a, NULL); pthread_join(b, NULL);
        CHECK("khong mat/lap/dao thu tu mau", stress_err == 0);
    }

    printf("4) stats.c (Welford)\n");
    stats_t st; stats_reset(&st);
    double v[6] = {2, 4, 4, 4, 5, 5};
    for (int i = 0; i < 6; i++) stats_add(&st, v[i]);
    CHECK("mean = 4", fabs(stats_mean(&st) - 4) < 1e-9);
    CHECK("std = sqrt(6/5)", fabs(stats_std(&st) - sqrt(1.2)) < 1e-9);
    CHECK("min = 2, max = 5", st.min == 2 && st.max == 5);
    printf("   sizeof(sample_t) = %zu byte, sizeof(ringbuf_t) = %zu byte\n", sizeof(sample_t), sizeof(ringbuf_t));

    printf(n_fail ? "\n==> %d kiem tra FAIL\n" : "\n==> Tat ca kiem tra OK\n", n_fail);
    return n_fail ? 1 : 0;
}

/* ringbuf.h – CE339 Lab TH2: bộ đệm vòng một-người-ghi / một-người-đọc (SPSC) cho mẫu có nhãn thời gian
 * Người ghi: task Lấy mẫu (core 1). Người đọc: task Xử lý (core 0). Không dùng khoá.
 * Dùng chung cho sketch.ino (ESP32) và test_ringbuf.c (PC, gcc + pthread).
 * Liên hệ Chương 2, Phần 3 "RTOS & jitter" và slide "Sơ đồ task firmware của nút máy bơm" (bộ đệm vòng 1024).
 */
#ifndef RINGBUF_H
#define RINGBUF_H
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RB_CAP 1024u                  /* dung lượng, BẮT BUỘC là lũy thừa của 2 */

typedef struct {
    uint32_t seq;                     /* số thứ tự mẫu */
    uint32_t t_us;                    /* nhãn thời gian micros() lúc lấy mẫu */
} sample_t;                           /* 8 byte */

typedef struct {
    sample_t buf[RB_CAP];
    volatile uint32_t head;           /* tổng số phần tử ĐÃ GHI (chỉ người ghi sửa) – đếm tự do, tràn số uint32 vẫn đúng */
    volatile uint32_t tail;           /* tổng số phần tử ĐÃ ĐỌC (chỉ người đọc sửa) */
    volatile uint32_t overflow;       /* số lần push thất bại vì đầy (chỉ người ghi sửa) */
} ringbuf_t;

void     rb_init(ringbuf_t *rb);
bool     rb_push(ringbuf_t *rb, sample_t s);    /* false nếu đầy (và tăng overflow) */
bool     rb_pop(ringbuf_t *rb, sample_t *out);  /* false nếu rỗng */
uint32_t rb_count(const ringbuf_t *rb);         /* số phần tử đang chờ đọc */

#ifdef __cplusplus
}
#endif
#endif

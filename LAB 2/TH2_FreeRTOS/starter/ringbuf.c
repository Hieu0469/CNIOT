/* ringbuf.c – CE339 Lab TH2: SV TỰ VIẾT các hàm có TODO.
 * Kiểm thử trên PC:  gcc -O2 -Wall -pthread -o test_ringbuf test_ringbuf.c ringbuf.c stats.c -lm && ./test_ringbuf
 * Quy ước: head = tổng số phần tử đã ghi, tail = tổng số phần tử đã đọc (đếm tự do, KHÔNG quay về 0).
 *          Số phần tử đang chờ = head - tail (phép trừ không dấu vẫn đúng khi tràn số uint32).
 *          Vị trí trong mảng = chỉ số & (RB_CAP - 1)  (vì RB_CAP là lũy thừa của 2).
 */
#include "ringbuf.h"

/* Rào bộ nhớ: dùng giữa "ghi dữ liệu vào buf" và "tăng head" (và tương tự phía đọc).
   Người đọc chạy trên core khác – vì sao cần rào? (câu hỏi vấn đáp) */
#define RB_BARRIER() __sync_synchronize()

void rb_init(ringbuf_t *rb) { rb->head = 0; rb->tail = 0; rb->overflow = 0; }

uint32_t rb_count(const ringbuf_t *rb) {
    /* TODO(1a) */
    (void)rb;
    return 0;
}

bool rb_push(ringbuf_t *rb, sample_t s) {
    /* TODO(1b): nếu đầy -> tăng overflow, trả về false (bỏ mẫu MỚI).
                 ngược lại: ghi s vào ô head & (RB_CAP-1), RB_BARRIER(), rồi mới tăng head; trả về true.
       Chỉ người GHI được sửa head và overflow. */
    (void)rb; (void)s;
    return false;
}

bool rb_pop(ringbuf_t *rb, sample_t *out) {
    /* TODO(1c): nếu rỗng -> false. Ngược lại đọc ô tail & (RB_CAP-1) vào *out, RB_BARRIER(), tăng tail; true.
       Chỉ người ĐỌC được sửa tail. */
    (void)rb; (void)out;
    return false;
}

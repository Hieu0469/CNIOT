/* noise.h – CE339 Lab TH1: mô hình nhiễu MÔ PHỎNG (đã viết sẵn, SV không cần sửa)
 * Wokwi cho giá trị cảm biến theo thanh trượt, gần như KHÔNG có nhiễu -> ta cộng nhiễu bằng code.
 * Bộ sinh số ngẫu nhiên chỉ dùng phép toán số nguyên (xorshift32 + tổng 12 số đều ~ Gauss)
 * nên cùng một hạt giống (seed = MSSV) cho CÙNG một chuỗi nhiễu trên ESP32 và trên PC.
 */
#ifndef NOISE_H
#define NOISE_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct { uint32_t s; } rng_t;

void    rng_seed(rng_t *r, uint32_t seed);
uint32_t rng_u32(rng_t *r);            /* xorshift32 */
float   rng_gauss(rng_t *r);            /* ~N(0,1): (tổng 12 số đều 16 bit - trung bình) / 65536 */

/* Nhiễu Gauss đã làm tròn về số nguyên LSB: round(sigma * g) */
int32_t noise_gauss_lsb(rng_t *r, float sigma_lsb);

/* Xung nhiễu tất định: mẫu n có xung biên độ amp nếu n % period == period/2 */
int32_t noise_spike(uint32_t n, uint32_t period, int32_t amp);

#ifdef __cplusplus
}
#endif
#endif

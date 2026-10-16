/* noise.c – CE339 Lab TH1: mô hình nhiễu mô phỏng (đã viết sẵn) */
#include "noise.h"

void rng_seed(rng_t *r, uint32_t seed) { r->s = seed ? seed : 2463534242u; }

uint32_t rng_u32(rng_t *r) {
    uint32_t x = r->s;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    r->s = x;
    return x;
}

float rng_gauss(rng_t *r) {
    /* Irwin–Hall: tổng 12 biến đều rời rạc 0..65535 có phương sai ~ 65536^2 -> chia 65536 được ~N(0,1).
       Mọi phép tính là số nguyên, phép chia cho 2^16 là chính xác trong float -> kết quả giống hệt mọi nền tảng. */
    int32_t sum = 0;
    for (int i = 0; i < 12; i++) sum += (int32_t)(rng_u32(r) >> 16);
    sum -= 393210;                      /* 12 * 32767.5 */
    return (float)sum / 65536.0f;
}

int32_t noise_gauss_lsb(rng_t *r, float sigma_lsb) {
    float v = sigma_lsb * rng_gauss(r);
    return (int32_t)(v >= 0 ? v + 0.5f : v - 0.5f);   /* làm tròn gần nhất, không cần libm */
}

int32_t noise_spike(uint32_t n, uint32_t period, int32_t amp) {
    if (period == 0) return 0;
    return (n % period == period / 2) ? amp : 0;
}

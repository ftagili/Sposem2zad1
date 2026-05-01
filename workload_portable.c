#include "workload.h"

uint64_t spo_asm_add_u64(uint64_t a, uint64_t b) {
    return a + b;
}

uint64_t spo_asm_double_u64(uint64_t x) {
    return x + x;
}

uint64_t spo_asm_mix_u64(uint64_t x, uint64_t y) {
    return (x ^ y) + 1u;
}

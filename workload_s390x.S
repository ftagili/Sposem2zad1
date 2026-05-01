    .text

    .globl spo_asm_add_u64
    .type spo_asm_add_u64, @function
spo_asm_add_u64:
    agr %r2, %r3
    br %r14
    .size spo_asm_add_u64, .-spo_asm_add_u64

    .globl spo_asm_double_u64
    .type spo_asm_double_u64, @function
spo_asm_double_u64:
    agr %r2, %r2
    br %r14
    .size spo_asm_double_u64, .-spo_asm_double_u64

    .globl spo_asm_mix_u64
    .type spo_asm_mix_u64, @function
spo_asm_mix_u64:
    xgr %r2, %r3
    aghi %r2, 1
    br %r14
    .size spo_asm_mix_u64, .-spo_asm_mix_u64

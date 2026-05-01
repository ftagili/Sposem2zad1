#!/usr/bin/env sh
set -eu

CC_BIN="${CC:-gcc}"
OUT_BIN="./spo_task1_v18"
CFLAGS="-std=c11 -O2 -Wall -Wextra -pedantic"

if [ "$(uname -m 2>/dev/null || echo unknown)" = "s390x" ]; then
  "$CC_BIN" $CFLAGS scheduler.c workload_s390x.S -o "$OUT_BIN"
else
  "$CC_BIN" $CFLAGS scheduler.c workload_portable.c -o "$OUT_BIN"
fi

"$OUT_BIN" --self-test > /tmp/spo_task1_selftest.out
grep -q "PASS" /tmp/spo_task1_selftest.out

"$OUT_BIN" --count 24 --seed 18 --no-trace > /tmp/spo_task1_run.out
grep -q "Execution mode: timer-driven user-space context scheduler" /tmp/spo_task1_run.out
grep -q "Best algorithm for this test: SRT" /tmp/spo_task1_run.out

"$OUT_BIN" --demo-threads --tick-us 10000 > /tmp/spo_task1_demo.out
grep -q "1111" /tmp/spo_task1_demo.out
grep -q "2222" /tmp/spo_task1_demo.out
awk '($0 == "1111" || $0 == "2222") { if ($0 == prev) repeated = 1; prev = $0 } END { exit repeated ? 0 : 1 }' /tmp/spo_task1_demo.out
grep -q "Demo finished" /tmp/spo_task1_demo.out
grep -q "createThread" demo_inputs/test1.src
grep -q "int PrintOnesProc" demo_inputs/test1.src
grep -q "int PrintTwosProc" demo_inputs/test1.src
grep -q "1111" demo_inputs/test1.src
grep -q "2222" demo_inputs/test1.src
grep -q "createThread" demo_inputs/test2.src
grep -q "int SrtSchedulerProc" demo_inputs/test2.src
grep -q "createThread" demo_inputs/test3.src
grep -q "int RoundRobinSchedulerProc" demo_inputs/test3.src

echo "task1_variant18: OK"

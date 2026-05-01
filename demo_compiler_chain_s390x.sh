#!/usr/bin/env sh
set -eu

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
COMPILER_DIR="${1:-${COMPILER_DIR:-}}"

if [ -z "${COMPILER_DIR}" ]; then
  for cand in \
    "$SCRIPT_DIR/../task3_source2ast" \
    "$SCRIPT_DIR/../source2ast" \
    "$SCRIPT_DIR/../source2ast-41e8146cc0107ad31cf76932afc772daa74b4e0c"
  do
    if [ -d "$cand" ] && [ -f "$cand/CMakeLists.txt" ] && [ -d "$cand/src/codegen" ]; then
      COMPILER_DIR="$cand"
      break
    fi
  done
fi

if [ -z "${COMPILER_DIR}" ] || [ ! -d "$COMPILER_DIR" ] || [ ! -f "$COMPILER_DIR/CMakeLists.txt" ]; then
  echo "ERROR: compiler project not found."
  echo "Expected source2ast project directory."
  echo ""
  echo "How to run:"
  echo "  ./demo_compiler_chain_s390x.sh /path/to/source2ast"
  echo ""
  echo "Or export environment variable:"
  echo "  export COMPILER_DIR=/path/to/source2ast"
  echo "  ./demo_compiler_chain_s390x.sh"
  echo ""
  echo "Note: SPO task1 itself works without source2ast."
  exit 2
fi

OUTPUT_DIR="$COMPILER_DIR/output/task1_variant18_demo"
INPUT_DIR="$SCRIPT_DIR/demo_inputs"

echo "[1/4] Build scheduler task"
cd "$SCRIPT_DIR"
make clean
make

echo "[2/4] Build compiler (source2ast)"
mkdir -p "$COMPILER_DIR/build"
cmake -S "$COMPILER_DIR" -B "$COMPILER_DIR/build"
cmake --build "$COMPILER_DIR/build"

echo "[3/4] Generate assembly files into output/"
if [ ! -d "$INPUT_DIR" ]; then
  echo "ERROR: demo input directory not found: $INPUT_DIR"
  exit 3
fi

mkdir -p "$OUTPUT_DIR"
generated_count=0
for src in "$INPUT_DIR"/*.src; do
  if [ ! -f "$src" ]; then
    continue
  fi
  base="$(basename "$src" .src)"
  if "$COMPILER_DIR/build/codegen" "$src" "$OUTPUT_DIR/$base.s"; then
    generated_count=$((generated_count + 1))
    echo "  OK: $base.s"
  else
    rm -f "$OUTPUT_DIR/$base.s"
    echo "  WARN: skip '$src' (syntax/codegen error in this source file)"
  fi
done

if [ "$generated_count" -eq 0 ]; then
  echo "ERROR: failed to generate any asm file from '$INPUT_DIR/*.src'"
  exit 4
fi

echo "[4/4] Run timer-driven scheduler report"
if ./spo_task1_v18 --count 24 --seed 18 --tick-us 1000 --no-trace > "$OUTPUT_DIR/scheduler_report.txt"; then
  :
else
  echo "  WARN: scheduler report step finished with non-zero status"
  echo "  WARN: generated asm files are still available in $OUTPUT_DIR"
fi

echo ""
echo "Done."
echo "Generated asm files count: $generated_count"
echo "Directory with asm files:"
echo "  $OUTPUT_DIR"
echo "Report:"
echo "  $OUTPUT_DIR/scheduler_report.txt"
echo "Runnable test1.src:"
echo "  $SCRIPT_DIR/run_test1_from_src_s390x.sh $COMPILER_DIR"

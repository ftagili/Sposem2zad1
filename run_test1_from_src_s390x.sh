#!/usr/bin/env sh
set -eu

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
COMPILER_DIR="${1:-${COMPILER_DIR:-}}"
PATCHED_CODEGEN="$SCRIPT_DIR/_inspect_source2ast/source2ast-41e8146cc0107ad31cf76932afc772daa74b4e0c/src/codegen/codegen.c"
INPUT_SRC="$SCRIPT_DIR/demo_inputs/test1.src"
OUTPUT_DIR="$SCRIPT_DIR/output/test1_from_src"

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
  echo "Run like this:"
  echo "  ./run_test1_from_src_s390x.sh /path/to/source2ast"
  exit 2
fi

TARGET_CODEGEN="$COMPILER_DIR/src/codegen/codegen.c"
BACKUP_CODEGEN="$TARGET_CODEGEN.bak.task1_variant18"

if [ ! -f "$TARGET_CODEGEN" ]; then
  echo "ERROR: compiler source file not found: $TARGET_CODEGEN"
  exit 3
fi

if [ ! -f "$PATCHED_CODEGEN" ]; then
  echo "ERROR: patched codegen template not found: $PATCHED_CODEGEN"
  exit 4
fi

echo "[1/5] Sync patched code generator"
if ! cmp -s "$PATCHED_CODEGEN" "$TARGET_CODEGEN"; then
  if [ ! -f "$BACKUP_CODEGEN" ]; then
    cp "$TARGET_CODEGEN" "$BACKUP_CODEGEN"
  fi
  cp "$PATCHED_CODEGEN" "$TARGET_CODEGEN"
  echo "  patched: $TARGET_CODEGEN"
else
  echo "  already patched"
fi

echo "[2/5] Build compiler (source2ast)"
mkdir -p "$COMPILER_DIR/build"
cmake -S "$COMPILER_DIR" -B "$COMPILER_DIR/build"
cmake --build "$COMPILER_DIR/build"

echo "[3/5] Generate assembly from test1.src"
mkdir -p "$OUTPUT_DIR"
"$COMPILER_DIR/build/codegen" "$INPUT_SRC" "$OUTPUT_DIR/test1.s"

echo "[4/5] Build runnable files"
cc -c -o "$OUTPUT_DIR/test1.o" "$OUTPUT_DIR/test1.s"
cc -std=c11 -O2 -Wall -Wextra -pedantic -c \
  -o "$OUTPUT_DIR/test1_runtime.o" "$SCRIPT_DIR/test1_runtime.c"
cc -no-pie \
  "$OUTPUT_DIR/test1.o" "$OUTPUT_DIR/test1_runtime.o" \
  -o "$OUTPUT_DIR/test1_from_src"

echo "  asm:    $OUTPUT_DIR/test1.s"
echo "  object: $OUTPUT_DIR/test1.o"
echo "  binary: $OUTPUT_DIR/test1_from_src"

echo "[5/5] Run generated program"
exec "$OUTPUT_DIR/test1_from_src"

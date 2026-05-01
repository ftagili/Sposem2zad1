set -euo pipefail

if [[ $# -ne 2 ]]; then
  echo "Usage: $0 <input_file> <output_file>" >&2
  exit 1
fi

in="$1"
out="$2"

ORIGINAL_DIR="$(pwd)"
if [[ "$in" != /* ]]; then
    in="$(cd "$(dirname "$in")" && pwd)/$(basename "$in")"
fi
if [[ "$out" != /* ]]; then
    out_dir="$(dirname "$out")"
    if [[ "$out_dir" != "." ]]; then
        mkdir -p "$out_dir"
        out="$(cd "$out_dir" && pwd)/$(basename "$out")"
    else
        out="$ORIGINAL_DIR/$(basename "$out")"
    fi
fi

# сборка проекта
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"
mkdir -p build
cd build
cmake ..
cmake --build .
cd ..

./build/codegen "$in" "$out"

# ассемблирование
gcc -c "$out" -o output/out.o -Wa,--noexecstack
gcc -no-pie output/out.o -o output/a.out
./output/a.out
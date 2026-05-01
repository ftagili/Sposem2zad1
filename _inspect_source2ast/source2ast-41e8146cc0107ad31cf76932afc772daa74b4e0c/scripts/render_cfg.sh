#!/bin/bash
# Script to render all DOT files to PNG images using Graphviz
# For Unix-like systems (macOS, Linux)

set -e

DOT_DIR="output"
IMG_DIR="output"

# Check if dot is available
if ! command -v dot &> /dev/null; then
    echo "Error: Graphviz (dot) not found in PATH" >&2
    echo "Please install Graphviz and ensure dot is in your PATH" >&2
    echo "On macOS: brew install graphviz" >&2
    echo "On Ubuntu/Debian: sudo apt-get install graphviz" >&2
    exit 1
fi

# Create image directory if it doesn't exist
mkdir -p "$IMG_DIR"

# Get all DOT files
DOT_FILES=("$DOT_DIR"/*.dot)

if [ ${#DOT_FILES[@]} -eq 0 ] || [ ! -f "${DOT_FILES[0]}" ]; then
    echo "Error: No DOT files found in $DOT_DIR" >&2
    exit 1
fi

echo "Found ${#DOT_FILES[@]} DOT file(s) to render..."

RENDERED=0
FAILED=0

# Process each DOT file
for dot_file in "${DOT_FILES[@]}"; do
    if [ ! -f "$dot_file" ]; then
        continue
    fi
    
    base_name=$(basename "$dot_file" .dot)
    img_file="$IMG_DIR/$base_name.png"
    
    echo "Rendering $dot_file to $img_file..."
    
    if dot -Tpng -Gdpi=120 "$dot_file" -o "$img_file" 2>/dev/null; then
        RENDERED=$((RENDERED + 1))
    else
        echo "Error: Failed to render $dot_file" >&2
        FAILED=$((FAILED + 1))
    fi
done

echo ""
echo "Summary:"
echo "  Rendered: $RENDERED image(s)"
echo "  Failed: $FAILED file(s)"

if [ $FAILED -gt 0 ]; then
    exit 1
fi

exit 0


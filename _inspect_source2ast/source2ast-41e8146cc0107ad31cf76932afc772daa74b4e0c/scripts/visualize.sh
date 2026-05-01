#!/bin/bash
set -e

# Создаем папку для выходных файлов
mkdir -p output

echo "Generating AST visualization for all test files..."
echo ""

# Обрабатываем каждый тестовый файл
for file in tests/ok/*.src; do
    if [ ! -f "$file" ]; then
        continue
    fi
    
    name=$(basename "$file" .src)
    echo "Generating AST for $name..."
    
    # Генерируем DOT-файл
    if ./build/semantic "$file" "output/${name}.dot"; then
        # Создаем PNG-изображение
        if dot -Tpng "output/${name}.dot" -o "output/${name}.png" 2>/dev/null; then
            echo "  ✓ Generated output/${name}.dot and output/${name}.png"
        else
            echo "  ⚠ Generated output/${name}.dot (PNG generation failed, check if dot is installed)"
        fi
    else
        echo "  ✗ Failed to generate AST for $name"
    fi
    echo ""
done

echo "All visualizations completed!"

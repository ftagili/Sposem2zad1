# Инструкция по запуску проекта source2ast

## Требования

- **CMake** версии 3.15 или выше
- **GCC** или **Clang** компилятор
- **Bison** (yacc) для генерации парсера
- **Flex** (lex) для генерации лексера
- **Graphviz** (опционально, для визуализации AST и CFG)

### Установка зависимостей

#### macOS
```bash
brew install cmake bison flex graphviz
```

#### Ubuntu/Debian
```bash
sudo apt-get update
sudo apt-get install build-essential cmake bison flex graphviz
```

#### Windows
- Установите [MSYS2](https://www.msys2.org/) или используйте WSL
- Установите CMake, GCC, Bison, Flex через пакетный менеджер

## Сборка проекта

### Базовая сборка

1. Перейдите в корневую директорию проекта:
```bash
cd /path/to/source2ast
```

2. Создайте директорию для сборки:
```bash
mkdir -p build
cd build
```

3. Запустите CMake:
```bash
cmake ..
```

4. Соберите проект:
```bash
cmake --build .
```

Или используйте make напрямую:
```bash
make
```

### Очистка сборки

Для полной очистки директории сборки и выходных файлов:
```bash
cd build
make clear
```

Или вручную:
```bash
rm -rf build/*
rm -rf output/*
```

## Использование инструментов

Проект содержит несколько исполняемых файлов, которые создаются в директории `build/`:

### 1. Парсер (parser)

Проверяет синтаксис исходного файла и строит AST.

**Использование:**
```bash
./build/parser <input-file>
```

**Пример:**
```bash
./build/parser tests/ok/test1.src
```

### 2. Семантический анализатор (semantic)

Анализирует AST и генерирует DOT-файл для визуализации.

**Использование:**
```bash
./build/semantic <input-file> <output-dot-file>
```

**Пример:**
```bash
./build/semantic tests/ok/test1.src output/test1.dot
```

**Визуализация AST:**
```bash
# Генерация PNG из DOT-файла
dot -Tpng output/test1.dot -o output/test1.png
```

### 3. Генератор CFG (cfg)

Строит граф потока управления для функций и генерирует DOT-файлы.

**Использование:**
```bash
./build/cfg <input-file>... [output-dir]
```

**Примеры:**
```bash
# Генерация CFG для одного файла
./build/cfg tests/ok/test1.src

# Генерация CFG для нескольких файлов в указанную директорию
./build/cfg tests/ok/test1.src tests/ok/test2.src output/

# CFG файлы будут созданы как: output/test1.func_name.cfg.dot
```

**Визуализация CFG:**
```bash
dot -Tpng output/test1.main.cfg.dot -o output/test1.main.cfg.png
```

### 4. Генератор кода (codegen)

Генерирует ассемблерный код для архитектуры s390x из AST.

**Использование:**
```bash
./build/codegen <input-file> <output-file>
```

или

```bash
./build/codegen <input-file> -o <output-file>
```

**Пример:**
```bash
# Генерация ассемблера
./build/codegen tests/ok/test1.src output/out.s

# Компиляция и запуск
gcc -c output/out.s -o output/out.o -Wa,--noexecstack
gcc -no-pie output/out.o -o output/a.out
./output/a.out
```

## Вспомогательные скрипты

### codegen.sh

Автоматически собирает проект, генерирует код, компилирует и запускает программу.

**Использование:**
```bash
./scripts/codegen.sh <input-file> <output-file>
```

**Пример:**
```bash
./scripts/codegen.sh tests/ok/test1.src output/out.s
```

### visualize.sh

Генерирует визуализации AST для всех тестовых файлов.

**Использование:**
```bash
./scripts/visualize.sh
```

Создаёт DOT и PNG файлы в директории `output/` для всех файлов из `tests/ok/*.src`.

### render_cfg.sh

Визуализирует CFG для всех функций в тестовых файлах.

**Использование:**
```bash
./scripts/render_cfg.sh
```

## Полный пример работы

### Шаг 1: Создание тестового файла

Создайте файл `test.src`:
```c
int add(int a, int b) {
    return a + b;
}

int main() {
    int x = 5;
    int y = 10;
    int result = add(x, y);
    return result;
}
```

### Шаг 2: Проверка синтаксиса
```bash
./build/parser test.src
```

### Шаг 3: Генерация AST
```bash
./build/semantic test.src output/test.dot
dot -Tpng output/test.dot -o output/test.png
```

### Шаг 4: Генерация CFG
```bash
./build/cfg test.src output/
dot -Tpng output/test.add.cfg.dot -o output/test.add.cfg.png
dot -Tpng output/test.main.cfg.dot -o output/test.main.cfg.png
```

### Шаг 5: Генерация кода
```bash
./build/codegen test.src output/test.s
```

### Шаг 6: Компиляция и запуск
```bash
gcc -c output/test.s -o output/test.o -Wa,--noexecstack
gcc -no-pie output/test.o -o output/test
./output/test
echo $?  # Выведет код возврата (15 в данном случае)
```

## Работа с классами и наследованием

Проект поддерживает объектно-ориентированные конструкции:

### Пример с классами

Создайте файл `class_test.src`:
```c
class Base {
    public int value;
    
    public int getValue() {
        return value;
    }
}

class Derived : Base {
    public int extra;
    
    public int getTotal() {
        return value + extra;
    }
}

int main() {
    Derived obj = new Derived();
    obj.value = 10;
    obj.extra = 5;
    return obj.getTotal();
}
```

### Обработка файла с классами

```bash
# Проверка синтаксиса
./build/parser class_test.src

# Генерация AST
./build/semantic class_test.src output/class_test.dot

# Генерация CFG (включая методы классов)
./build/cfg class_test.src output/

# Генерация кода
./build/codegen class_test.src output/class_test.s
```

## Структура выходных файлов

После выполнения инструментов в директории `output/` создаются:

- `*.dot` - файлы для визуализации AST и CFG
- `*.png` - изображения (если установлен Graphviz)
- `*.s` - ассемблерный код
- `*.o` - объектные файлы
- `a.out` или `test` - исполняемые файлы
- `*.cfg.dot` - CFG для отдельных функций

## Устранение проблем

### Ошибка: "bison not found"
Установите Bison:
```bash
# macOS
brew install bison

# Ubuntu
sudo apt-get install bison
```

### Ошибка: "flex not found"
Установите Flex:
```bash
# macOS
brew install flex

# Ubuntu
sudo apt-get install flex
```

### Ошибка компиляции парсера
Убедитесь, что `parser.tab.h` и `lex.yy.c` сгенерированы в директории `build/`:
```bash
cd build
ls parser.tab.h lex.yy.c
```

Если файлов нет, пересоберите:
```bash
cd build
cmake ..
make
```

### Ошибка при генерации DOT
Убедитесь, что Graphviz установлен:
```bash
# Проверка
which dot

# Установка (если отсутствует)
# macOS
brew install graphviz

# Ubuntu
sudo apt-get install graphviz
```

## Дополнительная информация

- Исходные файлы должны иметь расширение `.src`
- Поддерживаемые конструкции: функции, классы, наследование, поля, методы
- Генерируемый код предназначен для архитектуры s390x (IBM z/Architecture)
- Для компиляции сгенерированного кода требуется GCC с поддержкой s390x

## Быстрый старт (TL;DR)

```bash
# 1. Сборка
mkdir -p build && cd build
cmake ..
cmake --build .

# 2. Тест парсера
./parser ../tests/ok/test1.src

# 3. Генерация AST
./semantic ../tests/ok/test1.src ../output/test1.dot

# 4. Генерация кода
./codegen ../tests/ok/test1.src ../output/test1.s

# 5. Компиляция и запуск
cd ..
gcc -c output/test1.s -o output/test1.o -Wa,--noexecstack
gcc -no-pie output/test1.o -o output/test1
./output/test1
```


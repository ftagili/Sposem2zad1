# source2ast - Компилятор с поддержкой классов и наследования

## Быстрый старт

Для подробной инструкции по сборке и запуску см. [INSTRUCTIONS.md](INSTRUCTIONS.md)

### Быстрая сборка

```bash
mkdir -p build
cd build
cmake ..
cmake --build .
```

### Пример использования

```bash
# Генерация кода из исходного файла
./build/codegen tests/ok/test1.src output/out.s

# Компиляция и запуск
gcc -c output/out.s -o output/out.o -Wa,--noexecstack
gcc -no-pie output/out.o -o output/a.out
./output/a.out
```

## Задание 3

**Использование:**
```bash
./build/codegen <входной-файл>... [-o выходной-файл | выходная-директория]
```

**Примеры:**
```bash
./build/codegen test.src -o out.asm           # Вывод в файл
./build/codegen test.src -o out.asm --cfg     # + CFG файлы
./build/codegen test.src --cfg-only output/   # Только CFG
```

### Модель памяти

Виртуальная машина использует три сегмента памяти:

| Сегмент | Описание | Доступ |
|---------|----------|--------|
| **CODE** | Исполняемые инструкции | Только чтение |
| **CONSTANTS** | Литералы, строки, константы | Только чтение |
| **DATA** | Переменные, стек, куча | Чтение/запись |

```
+------------------+ 0x0000
|      CODE        | <- исполняемые инструкции
+------------------+ code_size
|    CONSTANTS     | <- литералы, строки
+------------------+ code_size + constants_size
|      DATA        | <- переменные, стек
+------------------+
```

### Регистры

| Регистр | Описание |
|---------|----------|
| **R0-R3** | Регистры общего назначения (R0 - аккумулятор) |
| **SP** | Указатель стека (Stack Pointer) |
| **BP** | Базовый указатель (Base Pointer) |
| **PC** | Счётчик команд (Program Counter) |
| **FLAGS** | Регистр флагов (ZF, SF, OF, CF) |

### Набор инструкций (двухадресный код)

Формат инструкций: `OP dest, src` где `dest := dest OP src`

| Категория | Инструкции | Описание |
|-----------|------------|----------|
| **Пересылка** | `MOV dest, src` | dest := src |
| | `LOAD dest, [addr]` | dest := DATA[addr] |
| | `STORE [addr], src` | DATA[addr] := src |
| | `LEA dest, addr` | dest := &addr (загрузка адреса) |
| | `LDC dest, const` | dest := CONSTANTS[const] |
| **Стек** | `PUSH src` | DATA[--SP] := src |
| | `POP dest` | dest := DATA[SP++] |
| **Арифметика** | `ADD dest, src` | dest := dest + src |
| | `SUB dest, src` | dest := dest - src |
| | `MUL dest, src` | dest := dest * src |
| | `DIV dest, src` | dest := dest / src |
| | `MOD dest, src` | dest := dest % src |
| | `NEG dest` | dest := -dest |
| | `INC dest`, `DEC dest` | dest := dest ± 1 |
| **Логические** | `AND dest, src` | dest := dest & src |
| | `OR dest, src` | dest := dest \| src |
| | `XOR dest, src` | dest := dest ^ src |
| | `NOT dest` | dest := ~dest |
| | `SHL dest, src` | dest := dest << src |
| | `SHR dest, src` | dest := dest >> src |
| **Сравнение** | `CMP op1, op2` | FLAGS := compare(op1, op2) |
| | `TEST op1, op2` | FLAGS := test(op1 & op2) |
| **Переходы** | `JMP label` | PC := label |
| | `JE label` | if (ZF) PC := label |
| | `JNE label` | if (!ZF) PC := label |
| | `JL label`, `JG label` | меньше/больше (знаковое) |
| | `JLE label`, `JGE label` | меньше-равно/больше-равно |
| | `JZ label`, `JNZ label` | ноль/не ноль |
| **Вызовы** | `CALL func` | вызов подпрограммы |
| | `RET` | возврат из подпрограммы |
| | `ENTER size` | создание стекового кадра |
| | `LEAVE` | разрушение стекового кадра |
| **Объявления** | `.func name` | начало функции |
| | `.endfunc` | конец функции |
| | `.data name, size` | объявление в DATA |
| | `.const name, val` | объявление в CONSTANTS |
| | `.param name` | объявление параметра |
| **Специальные** | `NOP` | нет операции |
| | `HLT` | останов |
| | `; text` | комментарий |

### Формат вывода

```asm
; ============================================================
; TWO-ADDRESS LINEAR CODE OUTPUT
; Generated from Control Flow Graph
; ============================================================
; Memory Model: CODE / CONSTANTS / DATA segments
; Instruction Format: OP dest, src (dest := dest OP src)
; Registers: R0-R3 (general), SP, BP, PC, FLAGS
; ============================================================

; ============================================================
; SEGMENT: CONSTANTS (read-only literals and strings)
; ============================================================
; (empty)

; ============================================================
; SEGMENT: DATA (variables and stack, read-write)
; ============================================================
; Stack: SP, BP managed at runtime

; ============================================================
; SEGMENT: CODE (executable instructions, read-only)
; ============================================================

; ============================================================
; Function: test_if
; Code address: 0x00000000, size: 57 bytes
; ============================================================
; --- DATA SECTION ---
; Stack frame size: 12 bytes
;
; Parameters (4 bytes):
;   x                : int       ; offset=0, size=4 bytes
;

.func test_if
    .param       x
; === PROLOGUE: Setup stack frame ===
    ENTER        0
L0:
    JMP          L2
L2:
    LOAD         R0, [BP+x]
    PUSH         R0
    MOV          R0, #0
    POP          R1
    CMP          R1, R0
    JZ           L4
    JMP          L3
L3:
    MOV          R0, #1
    RET         
L1:
; === EPILOGUE: Destroy stack frame ===
    LEAVE       
    RET         
.endfunc
```

### Модель выполнения

- **Двухадресный формат**: Результат сохраняется в первый операнд (dest)
- **Регистры R0-R3**: Используются для вычислений (R0 - основной аккумулятор)
- **Стек (DATA)**: Хранит промежуточные значения и локальные переменные
- **Адресация**: `[BP+var]` - индексная адресация относительно базового указателя
- **Сегменты**: CODE (код), CONSTANTS (константы), DATA (данные)

**Порядок вычисления бинарной операции:**
1. Левый операнд → R0
2. PUSH R0 → DATA[SP]  
3. Правый операнд → R0
4. POP R1 ← DATA[SP]
5. OP R1, R0 (R1 := R1 OP R0)
6. MOV R0, R1 (результат в R0)

**Вызов функции:**
1. Вычислить аргументы, PUSH в обратном порядке
2. CALL func
3. ADD SP, #args_size (очистка аргументов)

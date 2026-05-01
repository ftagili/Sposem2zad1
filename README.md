# SPO: задание 1, вариант 18

Реализация задания по планированию задач в многопоточной среде для варианта `18`.

- алгоритм 1: `SRT`;
- алгоритм 2: `RR(3)`;
- диапазон длительностей: `4..10`;
- средний интервал поступления: `10.5`;
- число пар `<время поступления, длительность>`: по умолчанию `24`.

Для чётного варианта `18` лучший алгоритм выбирается по минимальному среднему времени ожидания.

## Что важно для защиты

- Основной запуск на POSIX использует пользовательские контексты `ucontext`, отдельные стеки логических потоков и реальный таймер через `SIGALRM`/`setitimer`.
- Обработчик `SIGALRM` не создаёт системные потоки и не вызывает библиотечное планирование: он только отмечает наступление тика, после чего логический поток возвращает управление планировщику через `swapcontext`.
- В `--demo-threads` два пользовательских потока не печатают строго по одной строке за переключение: активный поток выдаёт несколько строк подряд, пока его не вытеснит таймер.
- В `demo_inputs/*.src` лежит программа на вашем языке из первого семестра, а не арифметические игрушки:
  - `test1.src` — демонстрация двух потоков с выводом `1111` и `2222`, причём теперь в этом же тексте есть отдельные процедуры `PrintOnesProc`, `PrintTwosProc` и `JoinPrinterResultsProc`;
  - `test2.src` — псевдокод запуска `SRT`, где рядом с `main` описаны `WorkloadSourceProc`, `SrtSchedulerProc` и `SchedulerReportProc`;
  - `test3.src` — псевдокод запуска `RR(3)`, где рядом с `main` описаны `WorkloadSourceProc`, `RoundRobinSchedulerProc` и `SchedulerReportProc`.
- `demo_compiler_chain_s390x.sh` прогоняет эти `.src` файлы через `source2ast` и складывает `.s` в `output/`.

## Состав

- `scheduler.c` — генератор нагрузки, модель и POSIX runtime, плюс демо вытесняемого переключения пользовательских потоков.
- `workload_s390x.S` — вспомогательные asm-функции для `s390x`.
- `workload_portable.c` — portable fallback.
- `test_s390x.sh` — быстрый прогон самопроверки и демо.
- `demo_compiler_chain_s390x.sh` — сборка `task1_variant18` + генерация asm из `demo_inputs/*.src`.

## Сборка

```bash
cd task1_variant18
make
```

## Запуск

Базовый запуск:

```bash
./spo_task1_v18
```

Таймерный запуск без подробной трассы:

```bash
./spo_task1_v18 --count 24 --seed 18 --tick-us 1000 --no-trace
```

Демо пользовательских потоков:

```bash
./spo_task1_v18 --demo-threads --tick-us 10000
```

Для защиты удобнее использовать `--tick-us 10000`: при таком тике вытеснение видно
нагляднее, и оба потока обычно успевают напечатать несколько строк до переключения.

Self-test:

```bash
./spo_task1_v18 --self-test
```

## Интеграция с `source2ast`

Скрипт:

1. собирает `task1_variant18`,
2. собирает проект `source2ast`,
3. генерирует `.s` файлы из `demo_inputs/*.src`,
4. сохраняет отчёт планировщика в тот же каталог `output/`.

```bash
./demo_compiler_chain_s390x.sh
```

Если проект компилятора лежит отдельно:

```bash
./demo_compiler_chain_s390x.sh /path/to/source2ast
```

## Быстрая проверка на `s390x`

```bash
./test_s390x.sh
```

## Runnable `test1.src`

To build and run the first-semester source version of `test1.src` as a real
program on the client machine:

```bash
./run_test1_from_src_s390x.sh /path/to/source2ast
```

This helper script:

1. copies the patched `codegen.c` into the selected `source2ast` tree,
2. rebuilds `source2ast`,
3. generates `output/test1_from_src/test1.s`,
4. builds `output/test1_from_src/test1.o`,
5. links the generated assembly with `test1_runtime.c`,
6. runs the resulting binary and prints `1111` / `2222`.

# TOY ISA 6 — Симулятор + DSL-ассемблер + JIT

Учебный проект по курсу «Симуляция ЦП и ОС».
Реализован с нуля: ассемблер на Ruby (DSL), симулятор на C++ с поддержкой ELF,
интерпретатор с BB-кэшем и динамическая двоичная трансляция (DBT) через AsmJit.


## Что умеет

### Ассемблер (Ruby DSL)
- Программы пишутся как Ruby-код: li r1, 42, add r3, r1, r2, beq r1, r2, :loop.
- Все 15 инструкций TOY ISA 6: LI, ADDI, ADD, MOVZ, MOVN, SELC, CLS, SSAT, SBIT,
  LD, ST, STP, BEQ, J, SYSCALL.
- Метки и fixups (разрешение адресов в save_elf).
- Генерация ELF32 (e_machine = 0xFF00, один PT_LOAD, p_vaddr = 0).
- Псевдоинструкции: mov (=li), hlt (=li r8, 93 + syscall), add r1, 5 (=addi).

### Симулятор (C++)
- Загрузка ELF через ELFIO с валидацией: ELF32, little-endian, e_machine == 0xFF00.
- Интерпретатор: decode -> execute, знаковое расширение setx<From,To>.
- BB-кэш: unordered_map<pc, BasicBlock>, exec_count для порога горячести.
- DBT (AsmJit): генерация x86-64 для каждого блока, кэш трансляций bt_cache.
- SMC: инвалидация трансляций при записи в код.
- Semihosting syscall: 1 = print_int, 2 = read_int, 64 = write, 93 = exit.
- ABI-независимость: #if defined(_WIN32) выбирает RCX (Windows) или RDI (Linux).


## Структура

dsl.rb                — Ruby-ассемблер (DSL + энкодер ELF32)
program.rb            — Пример программы на ассемблере
fib.rb                — Fibonacci с вводом N
bench.rb              — Бенчмарк: 3M инструкций, 1 горячий цикл
bench2.rb             — Бенчмарк: 50 блоков × 20K исполнений
bench3.rb             — Бенчмарк: hot (20K) + cold (100)

main_types.hpp        — Общие типы: Instruction, CpuState, Memory, setx<>
main.cpp              — decode, execute, run, syscall, exception, benchmark
generate_block.hpp    — Объявление generate_block
generate_block.cpp    — DBT: AsmJit-кодогенерация + C++-хелперы (dbt_ld, dbt_st, …)
elf_loader.hpp/.cpp   — Загрузка ELF через ELFIO

tests.cpp             — Unit-тесты (без gtest, свой мини-фреймворк)
elfio/                — Внешняя библиотека ELFIO (header-only)


## Сборка

### Требования
- MSYS2 с установленным mingw-w64-ucrt-x86_64-gcc и mingw-w64-ucrt-x86_64-asmjit.
- Ruby (>= 3.0).
- ELFIO — уже в репозитории (elfio/).

### Сборка симулятора
g++ main.cpp generate_block.cpp elf_loader.cpp -o a -std=c++17 -Wall -I. -lasmjit -O2 -DASMJIT_STATIC -Wno-invalid-offsetof

### Сборка тестов
g++ tests.cpp -o tests -std=c++17 -Wall -Wextra
./tests
Ожидаемо: 41 passed, 0 failed.


## Запуск

### 1. Собрать программу ассемблером
ruby fib.rb                    (создаёт output.elf)

### 2. Запустить интерпретатором
./a output.elf                 (ввод N -> вывод Fibonacci(N))

### 3. Запустить с DBT
./a output.elf --dbt --threshold 10

### 4. Флаги CLI
--bench           Полный бенчмарк (warmup + N замеров по всем порогам)
--interp          Только интерпретатор (без DBT)
--dbt             С DBT (по умолчанию)
--threshold N     Порог горячести (по умолчанию 10)
--runs N          Замеров на конфиг (по умолчанию 6)
--warmup N        Прогревов перед замерами (по умолчанию 2)


## Результаты тестирования

### Unit-тесты (tests.cpp)
- 41 проверка, покрытие: decode, execute (все 15 инструкций), исключения
  (misaligned, OOB), syscall.
- Изоляция тестов через new_cpu(&mem) — каждый тест на свежем CpuState.
- 0 failed.

### Self-checking (dsl_tests.rb)
- 16 тестов прямо на ассемблере: LI, ADD, ADDI, BEQ, J, LD/ST, STP, MOVZ, MOVN,
  CLS, SSAT, SELC, SBIT.
- Каждый тест пишет '1' (ok) или '0' (fail) в буфер, в конце выводятся 16 символов.
- Ожидаемо: 1111111111111111 — все 16 прошли.


## Бенчмарки (DBT vs интерпретатор)

Замер через std::chrono::high_resolution_clock, warmup=2, runs=6,
каждый замер на свежем JitRuntime (чтобы избежать накопления JIT-памяти).

### bench.elf — 3M инструкций, 1 горячий цикл
Интерпретатор      12.78 ms   1.00x
DBT (T=10)          4.49 ms   2.85x
DBT (T=25 000)      4.84 ms   2.64x
Плато — 99% работы в одном блоке. Порог влияет слабо.

### bench2.elf — 50 блоков × 20K исполнений
Интерпретатор      40.99 ms   1.00x
DBT (T=1)           7.17 ms   5.72x
DBT (T=100)         7.19 ms   5.70x
DBT (T=1000)        8.50 ms   4.82x
DBT (T=10000)      24.19 ms   1.69x
DBT (T=25000)      44.04 ms   0.93x

Плато 5.5-5.7x при T <= 1000. При T=25000 порог выше числа исполнений ->
ни один блок не транслируется -> полный откат к интерпретатору
(доказательство, что выигрыш даёт именно трансляция).

Проверка модели: для T из N=20000 исполнений скорость DBT-кода K≈5.7
(из плато). Модель Speedup = N/(T+(N−T)/K) предсказывает:
  T=10000 -> 1.70x   (замер: 1.69x, совпадение 99.4%)
  T=25000 -> 1.00x   (замер: 0.93x, overhead 7%)

### bench3.elf — hot (20K) + cold (100)
Смешанная нагрузка, где THRESHOLD — компромисс.
Оптимум при T=100-1000: hot-блоки транслируются, cold — нет.
При T=1 — overhead от трансляции cold.
При T=25000 — полный откат.


## Реализованные оптимизации

### Инлайнинг через AsmJit
- Все «простые» инструкции (LI, ADDI, ADD, MOVZ, MOVN, SELC, SBIT, BEQ, J,
  SYSCALL) генерируют x86-код напрямую (2-5 x86-инструкций на 1 Toy-инструкцию).
- «Сложные» (LD, ST, STP, CLS, SSAT) вызывают C++-хелперы (dbt_*)





# TOY-ISA Simulator

A cycle-accurate simulator for the TOY instruction set architecture (ISA) with a Ruby-based DSL assembler and an AsmJit-backed dynamic binary translator (DBT).

The project demonstrates the full toolchain: assembly source -> ELF binary -> CPU simulation (interpreter or JIT).

## Requirements

- C++17 compiler (GCC 11+ via MSYS2 UCRT64 or MinGW-w64)
- CMake 3.20+
- Ruby 3.0+
- AsmJit library (install via pacman -S mingw-w64-ucrt-x86_64-asmjit)

## Build & Run

### Build

Build the project in release mode:

    make all

Or directly through CMake:

    cmake -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
    cmake --build build

The simulator binary is placed at build/sim.

### Run

To verify the simulator's functionality, run the Fibonacci example:

1. Assemble the program using asm/dsl.rb:

       ruby asm/fib.rb

   This produces output.elf.

2. Run the simulator on the ELF file:

       printf '10\n' | ./build/sim output.elf

   The program reads N from stdin and prints Fibonacci(N) to stdout.

3. Run the same program with the JIT translator enabled:

       printf '10\n' | ./build/sim output.elf --dbt --threshold 10

## CLI flags

    --dbt             Enable dynamic binary translation (default)
    --interp          Use interpreter only (no JIT)
    --threshold N     Hotness threshold for JIT compilation (default 10)
    --bench           Run full benchmark suite (warmup + N timed runs)
    --warmup N        Number of warmup runs before timing (default 2)
    --runs N          Number of timed runs per configuration (default 6)

## Tests

Build and run unit tests for all ISA instructions:

    make tests

This compiles tests/tests.cpp and runs the test binary build/tests. Expected output: 41 passed, 0 failed.

## Project Structure

    toy-isa-sim/
      asm/                    Ruby DSL assembler + example programs
        dsl.rb                Encoder for all 15 instructions + ELF32 writer
        fib.rb                Fibonacci example
        bench.rb              Benchmark: single hot loop
        bench2.rb             Benchmark: 50 blocks x 20K executions
        dsl_tests.rb          Self-checking ISA tests
      include/sim/            Public C++ headers
      src/                    C++ sources (decoder, executor, JIT, ELF loader)
      tests/tests.cpp         Unit tests for decode + execute
      third_party/elfio/      ELFIO (header-only, MIT)
      CMakeLists.txt

## Benchmarks

All timings measured with std::chrono::high_resolution_clock on Windows (MinGW-w64 UCRT64), warmup = 2, 6 timed runs, median shown. Each run uses a fresh JitRuntime to avoid accumulated JIT code.

### bench.elf - 3M instructions, single hot loop

    Mode                      Median (ms)   Speedup
    Interpreter               12.78         1.00x
    DBT (threshold=1)          4.55         2.81x
    DBT (threshold=10)         4.49         2.85x
    DBT (threshold=1000)       4.57         2.80x
    DBT (threshold=25000)      4.84         2.64x

99% of the work happens inside a single basic block, so the threshold has little effect - the block is hot enough to be translated at any reasonable threshold.

### bench2.elf - 50 blocks x 20,000 executions each

    Mode                       Median (ms)   Speedup   Comment
    Interpreter                40.99         1.00x     baseline
    DBT (threshold=1)           7.17         5.72x     all blocks translated
    DBT (threshold=100)         7.19         5.70x     plateau
    DBT (threshold=1000)        8.50         4.82x     5% interpreted, 95% JIT
    DBT (threshold=10000)      24.19         1.69x     50% interpreted
    DBT (threshold=25000)      44.04         0.93x     threshold > N -> no translation

Model check. With N = 20000 executions per block and a JIT-vs-interpreter speed ratio K ~ 5.7, the predicted speedup is:

    Speedup = N / (T + (N - T) / K)

- For T = 10000: predicted 1.70x, measured 1.69x (99.4% match)
- For T = 25000: predicted 1.00x, measured 0.93x (7% overhead from threshold checks)

The collapse to 0.93x at T = 25000 confirms that all performance gains come from translation - once no block reaches the threshold, the simulator falls back to pure interpretation.

### bench3.elf - mixed workload (hot loop 20K + cold loop 100)

Demonstrates the threshold trade-off: low thresholds waste time translating cold code, high thresholds fail to translate hot code. Optimal range: T in [100, 1000].

## Implementation notes

- Interpreter: decode -> execute with switch dispatch, sign-extension via setx<From, To>.
- BB cache: unordered_map<pc, BasicBlock> with exec_count to track hotness.
- DBT (AsmJit): per-block x86-64 code generation, cached in bt_cache. Simple instructions are inlined; complex ones (LD, ST, STP, CLS, SSAT) call C++ helpers (dbt_*).
- SMC handling: writes to code memory invalidate any cached translation whose range overlaps the written address.
- Semihosting: syscall 1 (print int), 2 (read int), 64 (write buffer), 93 (exit).

## License

MIT. See LICENSE for details. ELFIO is distributed under the MIT license.
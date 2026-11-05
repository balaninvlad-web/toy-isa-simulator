#define UNIT_TESTS
#include "main.cpp"

static int g_passed = 0;
static int g_failed = 0;
static const char* g_current = "";

#define GROUP(name) do { g_current = name; std::cout << "[" << name << "]\n"; } while(0)

#define CHECK(cond) do {                                            \
    if (cond) { g_passed++; }                                       \
    else {                                                          \
        g_failed++;                                                 \
        std::cerr << "  FAIL  " << #cond                            \
                  << "  (" << g_current << ", line " << __LINE__ << ")\n"; \
    }                                                               \
} while(0)

#define CHECK_EQ(a, b) do {                                         \
    auto _a = (a); auto _b = (b);                                   \
    if (_a == _b) { g_passed++; }                                   \
    else {                                                          \
        g_failed++;                                                 \
        std::cerr << "  FAIL  " << #a << " == " << #b               \
                  << "  got " << _a << " vs " << _b                 \
                  << "  (" << g_current << ", line " << __LINE__ << ")\n"; \
    }                                                               \
} while(0)

// Хелпер: инструкция с нужным opcode
static Instruction mk(Opcode op) { Instruction i; i.op = op; return i; }

// Хелпер: свежий CpuState, привязанный к памяти
// Возврат по значению — компилятор сделает RVO (без копирования).
static CpuState new_cpu(Memory* mem) {
    CpuState c;
    c.mem = mem;
    return c;
}

void test_decode() {
    GROUP("decode");

    { auto d = decode(0x4408002Au);
      CHECK_EQ((int)d.op, (int)Opcode::kLi);
      CHECK_EQ((int)d.rt, 8);
      CHECK_EQ((int)d.imm, 42); }

    { word w = (1u<<21) | (2u<<16) | (3u<<11) | 0b110010u;
      auto d = decode(w);
      CHECK_EQ((int)d.op, (int)Opcode::kAdd);
      CHECK_EQ((int)d.rs, 1);
      CHECK_EQ((int)d.rt, 2);
      CHECK_EQ((int)d.rd, 3); }

    { word w = (0b111111u<<26) | (1u<<21) | (2u<<16) | 5u;
      auto d = decode(w);
      CHECK_EQ((int)d.op, (int)Opcode::kBeq);
      CHECK_EQ((int)d.rs, 1);
      CHECK_EQ((int)d.rt, 2);
      CHECK_EQ((int)d.imm, 5); }

    { word w = (0b101111u<<26) | 100u;
      auto d = decode(w);
      CHECK_EQ((int)d.op, (int)Opcode::kJ);
      CHECK_EQ(d.instr_index, 100u); }

    { auto d = decode(0b000001u << 26);
      CHECK_EQ((int)d.op, (int)Opcode::kUnknown); }
}

void test_arith() {
    GROUP("arith");

    Memory mem(1024);  

    // LI 42
    { auto c = new_cpu(&mem);
      auto i = mk(Opcode::kLi); i.rt = 1; i.imm = 42;
      execute(&c, i);
      CHECK_EQ(c.getReg(1), 42u);
      CHECK_EQ(c.pc, 4u); }

    // LI -1 (знаковое расширение)
    { auto c = new_cpu(&mem);
      auto i = mk(Opcode::kLi); i.rt = 1; i.imm = 0xFFFF;
      execute(&c, i);
      CHECK_EQ(c.getReg(1), 0xFFFFFFFFu); }

    // ADD 5 + 7
    { auto c = new_cpu(&mem);
      c.setReg(1, 5); c.setReg(2, 7);
      auto i = mk(Opcode::kAdd); i.rd = 3; i.rs = 1; i.rt = 2;
      execute(&c, i);
      CHECK_EQ(c.getReg(3), 12u); }

    // ADDI 5 + (-1)
    { auto c = new_cpu(&mem);
      c.setReg(1, 5);
      auto i = mk(Opcode::kAddi); i.rt = 2; i.rs = 1; i.imm = 0xFFFF;
      execute(&c, i);
      CHECK_EQ(c.getReg(2), 4u); }
}

void test_mov() {
    GROUP("movz/movn");

    Memory mem(1024);

    { auto c = new_cpu(&mem);
      c.setReg(1, 42); c.setReg(2, 0);
      auto i = mk(Opcode::kMovz); i.rd = 3; i.rs = 1; i.rt = 2;
      execute(&c, i);
      CHECK_EQ(c.getReg(3), 42u); }

    { auto c = new_cpu(&mem);
      c.setReg(1, 42); c.setReg(2, 5); c.setReg(3, 0);
      auto i = mk(Opcode::kMovz); i.rd = 3; i.rs = 1; i.rt = 2;
      execute(&c, i);
      CHECK_EQ(c.getReg(3), 0u); }

    { auto c = new_cpu(&mem);
      c.setReg(1, 42); c.setReg(2, 5);
      auto i = mk(Opcode::kMovn); i.rd = 3; i.rs = 1; i.rt = 2;
      execute(&c, i);
      CHECK_EQ(c.getReg(3), 42u); }
}

void test_memory() {
    GROUP("ld/st");

    // Запись + чтение
    { Memory mem(1024);         
      auto c = new_cpu(&mem);
      c.setReg(1, 0x12345678u);
      c.setReg(2, 100);
      auto st = mk(Opcode::kSt); st.base = 2; st.rt = 1; st.imm = 0;
      execute(&c, st);
      CHECK(!c.exception);

      c.setReg(3, 0);
      auto ld = mk(Opcode::kLd); ld.base = 2; ld.rt = 3; ld.imm = 0;
      execute(&c, ld);
      CHECK_EQ(c.getReg(3), 0x12345678u); }

    // Misaligned
    { Memory mem(1024);         
      auto c = new_cpu(&mem);
      c.setReg(1, 100);
      auto i = mk(Opcode::kLd); i.base = 1; i.rt = 2; i.imm = 3;
      execute(&c, i);
      CHECK(c.exception);
      CHECK_EQ((int)c.cause, (int)ExceptionCause::kMisalignedAccess);
      CHECK_EQ(c.pc, 0u); }

    // Out of bounds
    { Memory mem(1024);       
      auto c = new_cpu(&mem);
      c.setReg(1, 1020);
      auto i = mk(Opcode::kLd); i.base = 1; i.rt = 2; i.imm = 100;
      execute(&c, i);
      CHECK(c.exception);
      CHECK_EQ((int)c.cause, (int)ExceptionCause::kOutOfBounds); }
}

// ────────────────────────────────────────────────────────────
//  BRANCH
// ────────────────────────────────────────────────────────────
void test_branch() {
    GROUP("branch");

    Memory mem(1024);

    { auto c = new_cpu(&mem);
      c.setReg(1, 5); c.setReg(2, 5);
      auto i = mk(Opcode::kBeq); i.rs = 1; i.rt = 2; i.imm = 2;
      execute(&c, i);
      CHECK_EQ(c.pc, 8u); }

    { auto c = new_cpu(&mem);
      c.setReg(1, 5); c.setReg(2, 6);
      auto i = mk(Opcode::kBeq); i.rs = 1; i.rt = 2; i.imm = 2;
      execute(&c, i);
      CHECK_EQ(c.pc, 4u); }

    { auto c = new_cpu(&mem);
      c.pc = 0x50000000;
      auto i = mk(Opcode::kJ); i.instr_index = 4;
      execute(&c, i);
      CHECK_EQ(c.pc, 0x50000010u); }
}

void test_bitops() {
    GROUP("bitops");

    Memory mem(1024);

    // SSAT насыщает
    { auto c = new_cpu(&mem);
      c.setReg(1, 1000);
      auto i = mk(Opcode::kSsat); i.rd = 2; i.rs = 1; i.imm5 = 8;
      execute(&c, i);
      CHECK_EQ(c.getReg(2), 127u); }

    // CLS на всех единицах
    { auto c = new_cpu(&mem);
      c.setReg(1, 0xFFFFFFFFu);
      auto i = mk(Opcode::kCls); i.rd = 2; i.rs = 1;
      execute(&c, i);
      CHECK_EQ(c.getReg(2), 32u); }

    // CLS на нуле
    { auto c = new_cpu(&mem);
      c.setReg(1, 0);
      auto i = mk(Opcode::kCls); i.rd = 2; i.rs = 1;
      execute(&c, i);
      CHECK_EQ(c.getReg(2), 0u); }

    // SELC max
    { auto c = new_cpu(&mem);
      c.setReg(1, 5); c.setReg(2, 3);
      auto i = mk(Opcode::kSelc); i.rd = 3; i.rs1 = 1; i.rs2 = 2;
      execute(&c, i);
      CHECK_EQ(c.getReg(3), 5u); }
}

void test_syscall() {
    GROUP("syscall");

    Memory mem(1024);
    auto c = new_cpu(&mem);
    c.setReg(8, 93);

    auto i = mk(Opcode::kSyscall);
    execute(&c, i);

    CHECK(c.syscall_pending);
    CHECK_EQ(c.syscall_num, 93u);
    CHECK(!c.exception);
    CHECK_EQ(c.pc, 4u);

    handle_syscall(&c);
    CHECK(c.halted);
}

int main() {
    std::cout << "========== TESTS ==========\n";

    test_decode();
    test_arith();
    test_mov();
    test_memory();
    test_branch();
    test_bitops();
    test_syscall();

    std::cout << "\n===========================\n";
    std::cout << g_passed << " passed, " << g_failed << " failed\n";
    return g_failed == 0 ? 0 : 1;
}
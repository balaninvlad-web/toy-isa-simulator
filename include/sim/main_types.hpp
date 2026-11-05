#pragma once
#include <asmjit/x86.h>
#include <chrono>
#include <algorithm>
#include <cstdint>
#include <vector>
#include <unordered_map>
#include <iostream>
#include <cstddef>    
#include <string>
#include <cstring>
#include <fstream>
#include <iomanip>

using word = uint32_t;

enum class ExceptionCause 
{
    kNone,
    kMisalignedAccess,
    kIllegalInstruction,
    kOutOfBounds
};

enum class Opcode 
{
    kBeq, kSyscall, kLd, kSt, kMovz, 
    kAddi, kCls, kStp, kJ, kSsat, 
    kSelc, kAdd, kMovn, kLi, kSbit, kUnknown
};

struct Memory;

struct Instruction 
{
    Opcode op = Opcode::kUnknown;
    uint8_t rd = 0, rs = 0, rt = 0;
    uint8_t rs1 = 0, rs2 = 0, rt1 = 0, rt2 = 0, base = 0;
    int32_t imm = 0;
    uint32_t imm5 = 0;
    uint32_t instr_index = 0;
};

struct BasicBlock
{
    std::vector<Instruction> instrs;
    int exec_count = 0;
};

struct CpuState;

using JitFunc = void(*)(CpuState*);

struct DbtEntry
{
    JitFunc fn;
    word block_size;
};


struct CpuState
{
    word regs[32] = {};
    word pc = 0;
    Memory* mem = nullptr;
    bool syscall_pending = false;
    word syscall_num = 0;
    ExceptionCause cause = ExceptionCause::kNone;
    word exception_value = 0;
    bool halted = false;
    bool exception = false;
    word getReg (uint8_t index) const { return regs[index];}
    void setReg (uint8_t index, word value) { regs[index] = value;}
    void dump (bool full = false) const;
    std::unordered_map<word, BasicBlock> bb_cache;
    std::unordered_map<word, DbtEntry> bt_cache;
};

struct Memory
{
    std::vector<uint8_t> data;

    explicit Memory (size_t size) : data (size, 0) {}

    size_t size() const { return data.size(); }

    bool read32 (word addr, word& out) const //склеиваем так потому что хитровытраханный литлендиан
    {
        if (data.size() < 4 || addr > data.size() - 4) return false;
        std::memcpy(&out, &data[addr], 4);
        return true;
    }
    bool write32 (word addr, word value) 
    {
        if (data.size() < 4 || addr > data.size() - 4) return false; 
        std::memcpy(&data[addr], &value, 4);
        return true;
    }

    void load_binary (const std::string& path, word base, CpuState* cpu)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file)
        {
            std::cerr << "Cannot open " << path << "\n";
            cpu->halted = true;
            return;
        } 
        
        file.seekg(0, std::ios::end);
        size_t size = file.tellg();
        file.seekg(0);

        if (base + size > data.size())
        {
            std::cerr << "Binary too large for memory\n";
            cpu->halted = true;
            return;
        }

        file.read(reinterpret_cast<char*>(data.data() + base), size);
    }
};

template<int FromBits, int ToBits>
constexpr int32_t setx(uint32_t value)
{
    static_assert(FromBits > 0,         "FromBits must be > 0");
    static_assert(FromBits < ToBits,    "FromBits must be < ToBits");
    static_assert(ToBits <= 32,         "ToBits must be <= 32");

    constexpr uint32_t from_mask = (1u << FromBits) - 1u;
    value &= from_mask;
    constexpr uint32_t sign_bit = 1u << (FromBits - 1);
    if (value & sign_bit)
        return static_cast<int32_t>(value - (1u << FromBits));
    return static_cast<int32_t>(value);
}

const char* opcode_name(Opcode op);
bool isAligned(word addr);
Instruction decode(word encoding);
void execute(CpuState* cpu, const Instruction& instr);
void invalidate_smc (CpuState* cpu, word addr, word size = 4);
void handle_syscall(CpuState* cpu);
void handle_exception(CpuState* cpu);
void run(CpuState* cpu, bool use_dbt = true, int threshold = 10);
BasicBlock& build_bb (CpuState* cpu, word entry_pc);
constexpr int32_t setx(uint32_t value);

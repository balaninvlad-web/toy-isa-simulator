#include <asmjit/x86.h>
#include <iostream>
#include "main_types.hpp"


#define R10D asmjit::x86::r10d
#define R11D asmjit::x86::r11d
#define R11  asmjit::x86::r11

// Память: смещение от cpu_ptr
#define REG_MEM(i) asmjit::x86::dword_ptr(cpu_ptr, REGS_OFF + (i) * 4)
#define PC_MEM     asmjit::x86::dword_ptr(cpu_ptr, PC_OFF)


// Увеличить PC на 4 — обычное поведение
#define PC_NEXT() a.add(PC_MEM, 4)


#if defined(_WIN32)
    #define ARG_CPU asmjit::x86::rcx
    #define ARG2    asmjit::x86::rdx
    #define ARG3    asmjit::x86::r8
    #define ARG4    asmjit::x86::r9
#else
    #define ARG_CPU asmjit::x86::rdi
    #define ARG2    asmjit::x86::rsi
    #define ARG3    asmjit::x86::rdx
    #define ARG4    asmjit::x86::rcx
#endif

#define CALL_CXX(fn_ptr) do { \
    a.mov(R11, (uint64_t)(fn_ptr)); \
    a.call(R11); \
} while(0)

using JitFunc = void(*)(CpuState*);

extern "C" void dbt_ld (CpuState* cpu, word rt, word base, word imm)
{
    word addr = cpu->getReg(base) + imm;
    if (!isAligned(addr)) {
        cpu->exception = true;
        cpu->cause = ExceptionCause::kMisalignedAccess;
        cpu->exception_value = addr;
        return;
    }
    word value = 0;
    if (!cpu->mem->read32(addr, value)) {
        cpu->exception = true;
        cpu->cause = ExceptionCause::kOutOfBounds;
        cpu->exception_value = addr;
        return;
    }
    cpu->setReg(rt, value);
    cpu->pc += 4;
}

extern "C" void dbt_st (CpuState* cpu, word rt, word base, word imm)
{
    word addr = cpu->getReg(base) + imm;
    if (!isAligned(addr)) {
        cpu->exception = true;
        cpu->cause = ExceptionCause::kMisalignedAccess;
        cpu->exception_value = addr;
        return;
    }
    if (!cpu->mem->write32(addr, cpu->getReg(rt))) {
        cpu->exception = true;
        cpu->cause = ExceptionCause::kOutOfBounds;
        cpu->exception_value = addr;
        return;
    }
    invalidate_smc(cpu, addr, 4);
    cpu->pc += 4;
}

extern "C" void dbt_stp (CpuState* cpu, word rt1, word rt2, word packed)
{
    word base = (packed >> 16) & 0xFF;
    int32_t offset = static_cast<int16_t>(packed & 0xFFFF);

    word addr = cpu->getReg(base) + offset;
    if (!isAligned(addr)) {
        cpu->exception = true;
        cpu->cause = ExceptionCause::kMisalignedAccess;
        cpu->exception_value = addr;
        return;
    }

    if (!cpu->mem->write32(addr, cpu->getReg(rt1))) {
        cpu->exception = true;
        cpu->cause = ExceptionCause::kOutOfBounds;
        cpu->exception_value = addr;
        return;
    }

    if (!cpu->mem->write32(addr + 4, cpu->getReg(rt2))) {
        cpu->exception = true;
        cpu->cause = ExceptionCause::kOutOfBounds;
        cpu->exception_value = addr;
        return;
    }
    invalidate_smc(cpu, addr, 8);
    cpu->pc += 4;
}

extern "C" void dbt_cls(CpuState* cpu, word rd, word rs)
{
    word value = cpu->getReg(rs);
    int count = 0;
    if (value & 0x80000000u) 
        while (count < 32 && (value & (1u << (31 - count)))) ++count;

    cpu->setReg(rd, static_cast<word>(count));
    cpu->pc += 4;
}

extern "C" void dbt_ssat(CpuState* cpu, word rd, word rs, word n)
{
    int32_t value = static_cast<int32_t>(cpu->getReg(rs));
    if (n == 0 || n >= 32) {
        cpu->setReg(rd, static_cast<word>(value));
        cpu->pc += 4;
        return;
    }
    int32_t max_val = (1 << (n - 1)) - 1;
    int32_t min_val = -(1 << (n - 1));
    if (value > max_val) value = max_val;
    if (value < min_val) value = min_val;
    cpu->setReg(rd, static_cast<word>(value));
    cpu->pc += 4;
}
JitFunc generate_block (CpuState* cpu, asmjit::JitRuntime& rt, const std::vector<Instruction>& block)
{
    asmjit::CodeHolder code;
    code.init(rt.environment());
    asmjit::x86::Assembler a(&code);

    asmjit::x86::Gp cpu_ptr = ARG_CPU;

    const word REGS_OFF = offsetof(CpuState, regs);
    const word PC_OFF   = offsetof(CpuState, pc);

    bool terminated = false;

    for (const auto& instr : block)
    {
        if (terminated) break;

        switch (instr.op)
        {
            case Opcode::kLi:{
                a.mov(REG_MEM(instr.rt), (word)(int16_t)(instr.imm));
                PC_NEXT();
                break;
            }
            case Opcode::kAddi:{
                a.mov(R10D, REG_MEM(instr.rs));
                a.add(R10D, (word)(int16_t)(instr.imm));
                a.mov(REG_MEM(instr.rt), R10D);
                PC_NEXT();
                break;
            }
            case Opcode::kAdd:{
                a.mov(R10D, REG_MEM(instr.rs));
                a.add(R10D, REG_MEM(instr.rt));
                a.mov(REG_MEM(instr.rd), R10D);
                PC_NEXT();
                break;
            }
            case Opcode::kMovz:{
                a.mov(R10D, REG_MEM(instr.rt));
                a.test(R10D, R10D);
                asmjit::Label L_skip = a.new_label();
                a.jne(L_skip);                
                a.mov(R10D, REG_MEM(instr.rs));
                a.mov(REG_MEM(instr.rd), R10D);
                a.bind(L_skip);
                PC_NEXT();
                break;
            }
            case Opcode::kMovn:{
                a.mov(R10D, REG_MEM(instr.rt));
                a.test(R10D, R10D);
                asmjit::Label L_skip = a.new_label();
                a.je(L_skip);                
                a.mov(R10D, REG_MEM(instr.rs));
                a.mov(REG_MEM(instr.rd), R10D);
                a.bind(L_skip);
                PC_NEXT();
                break;
            }
            case Opcode::kSelc:{
                a.mov(R10D, REG_MEM(instr.rs1));
                a.mov(R11D, REG_MEM(instr.rs2));
                a.cmp(R10D, R11D);
                a.cmovl(R10D, R11D);              
                a.mov(REG_MEM(instr.rd), R10D);
                PC_NEXT();
                break;
            }
            case Opcode::kSbit:
                a.mov(REG_MEM(instr.rd), (word)(1u << instr.imm5));
                PC_NEXT();
                break;
            case Opcode::kBeq:{
                a.mov(R10D, REG_MEM(instr.rs));
                a.cmp(R10D, REG_MEM(instr.rt));
            
                asmjit::Label L_not_taken = a.new_label();
                asmjit::Label L_done = a.new_label();

                a.jne(L_not_taken);

                int32_t target = setx<16, 32>(instr.imm) * 4;
                a.add(PC_MEM, target);
                a.jmp(L_done);

                a.bind(L_not_taken);
                a.add(PC_MEM, 4);

                a.bind(L_done);
                terminated = true;
                break;
            }
            case Opcode::kJ:{
                a.mov(R10D, PC_MEM);
                a.and_(R10D, 0xF0000000u);
                a.or_(R10D, (word)(instr.instr_index << 2));
                a.mov(PC_MEM, R10D);
                terminated = true;
                break;
            }
            case Opcode::kSyscall:{
                a.mov(asmjit::x86::byte_ptr(cpu_ptr, offsetof(CpuState, syscall_pending)), (uint8_t)1);
                a.mov(R10D, REG_MEM(8));
                a.mov(asmjit::x86::dword_ptr(cpu_ptr, offsetof(CpuState, syscall_num)), R10D);
                PC_NEXT();
                terminated = true;
                break;
            }
            case Opcode::kCls:{
                a.mov(ARG2, (word)instr.rd);
                a.mov(ARG3, (word)instr.rs);
                CALL_CXX(&dbt_cls);
                break;
            }
            case Opcode::kSsat:{
                a.mov(ARG2, (word)instr.rd);
                a.mov(ARG3, (word)instr.rs);
                a.mov(ARG4, (word)instr.imm5);
                CALL_CXX(&dbt_ssat);
                break;
            }
            case Opcode::kLd:{
                a.mov(ARG2, (word)instr.rt);
                a.mov(ARG3, (word)instr.base);
                a.mov(ARG4, (word)setx<14, 32>(instr.imm));
                CALL_CXX(&dbt_ld);
                break;
            }
            case Opcode::kSt:{
                a.mov(ARG2, (word)instr.rt);
                a.mov(ARG3, (word)instr.base);
                a.mov(ARG4, (word)setx<14, 32>(instr.imm));
                CALL_CXX(&dbt_st);
                break;
            }
            case Opcode::kStp:{
                word base_and_offset = ((word)instr.base << 16) | ((word)setx<11, 32>(instr.imm) & 0xFFFF);
                a.mov(ARG2, (word)instr.rt1);
                a.mov(ARG3, (word)instr.rt2);
                a.mov(ARG4, base_and_offset);
                CALL_CXX(&dbt_stp);
                break;
            }
            default:
                break;
        }
    }
    a.ret();

    JitFunc fn = nullptr;
    asmjit::Error err = rt.add(&fn, &code);
    if (err != asmjit::Error::kOk)
    {
        std::cerr << "Asmjit error: " << asmjit::DebugUtils::error_as_string(err) << "\n";
        return nullptr;
    }
    return fn;
}
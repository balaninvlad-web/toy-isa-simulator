#include "main_types.hpp"
#include "elf_loader.hpp"

using JitFunc = void(*)(CpuState*);
JitFunc generate_block(CpuState* cpu, asmjit::JitRuntime& rt,
                       const std::vector<Instruction>& block);

const char* opcode_name(Opcode op) 
{
    switch (op) 
    {
        case Opcode::kBeq:      return "BEQ";
        case Opcode::kSyscall:  return "SYSCALL";
        case Opcode::kLd:       return "LD";
        case Opcode::kSt:       return "ST";
        case Opcode::kMovz:     return "MOVZ";
        case Opcode::kAddi:     return "ADDI";
        case Opcode::kCls:      return "CLS";
        case Opcode::kStp:      return "STP";
        case Opcode::kJ:        return "J";
        case Opcode::kSsat:     return "SSAT";
        case Opcode::kSelc:     return "SELC";
        case Opcode::kAdd:      return "ADD";
        case Opcode::kMovn:     return "MOVN";
        case Opcode::kLi:       return "LI";
        case Opcode::kSbit:     return "SBIT";
        default:                return "UNKNOWN";
    }
}

bool isAligned(word addr) { return (addr & 0x3) == 0; }

void CpuState::dump(bool full) const
{
    std::cout << "==========<(CPU dump)>===========\n";
    std::cout << "PC = 0x" << std::hex << std::setw(8) << std::setfill('0')
              << pc << std::dec << "  (" << pc << ")\n";

    std::cout << "halted = "    << (halted    ? "true" : "false") << "\n";
    std::cout << "exception = " << (exception ? "true" : "false") << "\n";

    if (exception || cause != ExceptionCause::kNone) 
    {
        std::cout << "cause = " << static_cast<int>(cause)
                  << "  value = 0x" << std::hex << exception_value
                  << std::dec << "\n";
    }

    std::cout << "\nRegisters:\n";
    for (int row = 0; row < 4; ++row) 
    {
        for (int col = 0; col < 8; ++col) 
        {
            int i = row * 8 + col;
            std::cout << "r" << std::setw(2) << std::setfill(' ') << i << " = "
                      << std::hex << std::setw(8) << std::setfill('0')
                      << regs[i] << std::dec << "  ";
        }
        std::cout << "\n";
    }

    if (full && mem) 
    {
        std::cout << "\nMemory around PC (0x"
                  << std::hex << pc << "):\n" << std::dec;

        word start = (pc >= 16) ? pc - 16 : 0;
        for (word addr = start; addr < start + 64; addr += 4) 
        {
            if (addr + 4 > mem->size()) break;

            // Читаем байты напрямую — не трогаем флаги CPU.
            word val = static_cast<word>(mem->data[addr])
                     | (static_cast<word>(mem->data[addr + 1]) << 8)
                     | (static_cast<word>(mem->data[addr + 2]) << 16)
                     | (static_cast<word>(mem->data[addr + 3]) << 24);

            std::cout << "  [0x" << std::hex << std::setw(8) << std::setfill('0')
                      << addr << "] = 0x" << std::setw(8) << val
                      << std::dec << "\n";
        }
    }

    if (full && !bb_cache.empty())
    {
        std::cout << "\nBB cache(size" << bb_cache.size() <<" blocks):\n";
        
        for (const auto& [addr, bb] : bb_cache)
        {
            std::cout << " [0x" 
                      << std::hex <<std::setw(8) << std::setfill('0') 
                      << addr << "]" << std::dec 
                      << "  " << bb.instrs.size() << "instr(s)\n";
            
            for (size_t i = 0; i < bb.instrs.size(); ++i)
            {
                const auto& ins = bb.instrs[i];
                word insn_pc = addr + static_cast<word>(i * 4); 

                std::cout << "      0x" << std::hex << std::setw(8)
                          << std::setfill('0') << insn_pc
                          << std::setfill(' ') << std::dec << "  "
                          << std::setw(8) << std::left << opcode_name(ins.op)
                          << std::right;

            if (ins.rd || ins.rs || ins.rt) {
                    std::cout << " rd=" << (int)ins.rd
                              << " rs=" << (int)ins.rs
                              << " rt=" << (int)ins.rt;
                }
                if (ins.imm) {
                    std::cout << " imm=" << ins.imm;
                }
                if (ins.imm5) {
                    std::cout << " imm5=" << ins.imm5;
                }
                if (ins.base) {
                    std::cout << " base=" << (int)ins.base;
                }
                if (ins.rt1 || ins.rt2) {
                    std::cout << " rt1=" << (int)ins.rt1
                              << " rt2=" << (int)ins.rt2;
                }
                if (ins.rs1 || ins.rs2) {
                    std::cout << " rs1=" << (int)ins.rs1
                              << " rs2=" << (int)ins.rs2;
                }
                if (ins.instr_index) {
                    std::cout << " idx=" << ins.instr_index;
                }
                std::cout << "\n";
            }
        }

    }

    std::cout << "==============================\n";
}

Instruction decode(word encoding)
{
    Instruction instr;
    word opcode6 = (encoding >> 26) & 0x3F;
    word opcode5 = encoding & 0x3F;

    switch (opcode6) //opcode
    {
        case 0b111111: //BEQ
            instr.op = Opcode::kBeq;
            instr.rs = (encoding >> 21) & 0x1F;
            instr.rt = (encoding >> 16) & 0x1F;
            instr.imm = encoding & 0xFFFF;
            break;
        case 0b000000: //syscall, movz, cls, Selc, add, movn
            switch (opcode5)
            {
                case 0b011101:
                    instr.op =  Opcode::kSyscall;
                    break;
                case 0b011100:
                    instr.op =  Opcode::kMovz;
                    instr.rs =  (encoding >> 21) & 0x1F;
                    instr.rt =  (encoding >> 16) & 0x1F;
                    instr.rd =  (encoding >> 11) & 0x1F;
                    break;
                case 0b110011:
                    instr.op =  Opcode::kCls;
                    instr.rd =  (encoding >> 21) & 0x1F;
                    instr.rs =  (encoding >> 16) & 0x1F;
                    break;
                case 0b010000:
                    instr.op =  Opcode::kSelc;
                    instr.rd =  (encoding >> 21) & 0x1F;
                    instr.rs1 = (encoding >> 16) & 0x1F;
                    instr.rs2 = (encoding >> 11) & 0x1F;
                    break;
                case 0b110010:
                    instr.op =  Opcode::kAdd;
                    instr.rs =  (encoding >> 21) & 0x1F;
                    instr.rt =  (encoding >> 16) & 0x1F;
                    instr.rd =  (encoding >> 11) & 0x1F;
                    break;
                case 0b000101:
                    instr.op =  Opcode::kMovn;
                    instr.rs =  (encoding >> 21) & 0x1F;
                    instr.rt =  (encoding >> 16) & 0x1F;
                    instr.rd =  (encoding >> 11) & 0x1F;
                    break;
                default:
                    instr.op = Opcode::kUnknown;
                    break;
            }
            break;
        case 0b010011: //Ld
            instr.op = Opcode::kLd;
            instr.base = (encoding >> 21) & 0x1F;
            instr.rt = (encoding >> 16) & 0x1F;
            instr.imm = encoding & 0x3FFF;
            break;
        case 0b011110: //St
            instr.op = Opcode::kSt;
            instr.base = (encoding >> 21) & 0x1F;
            instr.rt = (encoding >> 16) & 0x1F;
            instr.imm = encoding & 0x3FFF;
            break;
        case 0b111001: //addi
            instr.op = Opcode::kAddi;
            instr.rs = (encoding >> 21) & 0x1F;
            instr.rt = (encoding >> 16) & 0x1F;
            instr.imm = encoding & 0xFFFF;
            break;
        case 0b001110: // stp
            instr.op = Opcode::kStp;
            instr.base = (encoding >> 21) & 0x1F;
            instr.rt1 = (encoding >> 16) & 0x1F;
            instr.rt2 = (encoding >> 11) & 0x1F;
            instr.imm = encoding & 0x7FF; //offset
            break; 
        case 0b101111: //J
            instr.op = Opcode::kJ;
            instr.instr_index = encoding & 0x3FFFFFF;
            break;
        case 0b111100: //Ssat
            instr.op = Opcode::kSsat;
            instr.rd = (encoding >> 21) & 0x1F;
            instr.rs = (encoding >> 16) & 0x1F;
            instr.imm5 = (encoding >> 11) & 0x1F;
            break;
        case 0b010001: //Li
            instr.op = Opcode::kLi;
            instr.rt = (encoding >> 16) & 0x1F;
            instr.imm = encoding & 0xFFFF;
            break;
        case 0b010010: //Sbit
            instr.op = Opcode::kSbit;
            instr.rd = (encoding >> 21) & 0x1F;
            instr.rs = (encoding >> 16) & 0x1F;
            instr.imm5 = (encoding >> 11) & 0x1F;
            break;
        default:
            instr.op = Opcode::kUnknown;
            break;
    }
    return instr;
}

void execute (CpuState* cpu, const Instruction& instr)
{
    word old_pc = cpu->pc;
    bool pc_updated = false;

    switch (instr.op)
    {
        case Opcode::kBeq:
        {
            int32_t offset = setx<16, 32>(instr.imm);
            int32_t target = offset * 4;
            if (cpu->getReg(instr.rs) == cpu->getReg(instr.rt))
                cpu->pc = cpu->pc + target;
            else
                cpu->pc += 4;

            pc_updated = true;
            break;
        }
        case Opcode::kSyscall:
        {
            cpu->syscall_pending = true;
            cpu->syscall_num = cpu->getReg(8); //x8 - num of syscall
            break;
        }
        case Opcode::kLd:
        {
            word addr = cpu->getReg(instr.base) + setx<14, 32>(instr.imm);
            if (!isAligned(addr))
            {
                cpu->exception = true;
                cpu->cause = ExceptionCause::kMisalignedAccess;
                cpu->exception_value = addr;
                pc_updated = true; 
                break;
            }
            word value;
            if (!cpu->mem->read32(addr, value)) 
            {
                cpu->exception = true;
                cpu->cause = ExceptionCause::kOutOfBounds;
                cpu->exception_value = addr;
                pc_updated = true;
                break;
            }
            cpu->setReg(instr.rt, value);
            break;
        }
        case Opcode::kSt:
        {
            word addr = cpu->getReg(instr.base) + setx<14, 32>(instr.imm);
            if (!isAligned(addr))
            {
                cpu->exception = true;
                cpu->cause = ExceptionCause::kMisalignedAccess;
                cpu->exception_value = addr;
                pc_updated = true;
                break;
            }
            if(!cpu->mem->write32(addr, cpu->getReg(instr.rt)))
            {
                cpu->exception = true;
                cpu->cause = ExceptionCause::kOutOfBounds;
                cpu->exception_value = addr;
                pc_updated = true;
                break;
            }
            invalidate_smc(cpu, addr, 4);
            break;
        }
        case Opcode::kMovz:
        {
            if (cpu->getReg(instr.rt) == 0)
                cpu->setReg(instr.rd, cpu->getReg(instr.rs));
            break;
        }
        case Opcode::kAddi:
        {
            cpu->setReg(instr.rt, (cpu->getReg(instr.rs) + setx<16, 32>(instr.imm)));
            break;
        }
        case Opcode::kCls:
        {
            word value = cpu->getReg(instr.rs);
            int count = 0; 
            if (value & 0x80000000)
                while (count < 32 && (value & (1U << (31 - count)))) ++count;
            else    
                count = 0;
            cpu->setReg(instr.rd, count);
            break;
        }
        case Opcode::kStp:
        {
            word addr = cpu->getReg(instr.base) + setx<11, 32>(instr.imm);
            if (!isAligned(addr))
            {
                cpu->exception = true;
                cpu->cause = ExceptionCause::kMisalignedAccess;
                cpu->exception_value = addr;
                pc_updated = true;
                break;
            }
            if (!cpu->mem->write32(addr, cpu->getReg(instr.rt1))) 
            {
                cpu->exception = true;
                cpu->cause = ExceptionCause::kOutOfBounds;
                cpu->exception_value = addr;
                pc_updated = true;
                break;
            }
            if (!cpu->mem->write32(addr + 4, cpu->getReg(instr.rt2)))
            {
                cpu->exception = true;
                cpu->cause = ExceptionCause::kOutOfBounds;
                cpu->exception_value = addr;
                pc_updated = true;
                break;
            }
            invalidate_smc(cpu, addr, 8);
            break;
        }
        case Opcode::kJ:
        {
            cpu->pc = (cpu->pc & 0xF0000000) | (instr.instr_index << 2);
            pc_updated = true;
            break;
        }
        case Opcode::kSsat:
        {
            int32_t value = static_cast<int32_t>(cpu->getReg(instr.rs));
            uint32_t n = instr.imm5;
            if (n == 0 || n >= 32)
            {
                cpu->setReg(instr.rd, value);
                break;
            }
            int32_t max_val = (1 << (n - 1)) - 1;
            int32_t min_val = -(1 << (n - 1));
            if (value > max_val) value = max_val;
            if (value < min_val) value = min_val;
            cpu->setReg(instr.rd, static_cast<word>(value));
            break;
        }
        case Opcode::kSelc:
        {
            int32_t a = static_cast<int32_t>(cpu->getReg(instr.rs1));
            int32_t b = static_cast<int32_t>(cpu->getReg(instr.rs2));
            cpu->setReg(instr.rd, (a > b) ? a : b);
            break;
        }
        case Opcode::kAdd:
        {
            cpu->setReg(instr.rd, static_cast<word>((cpu->getReg(instr.rs)) + (cpu->getReg(instr.rt))));
            break;
        }
        case Opcode::kMovn:
        {
            if ((cpu->getReg(instr.rt)) != 0) 
                cpu->setReg(instr.rd, cpu->getReg(instr.rs));
            break;
        }
        case Opcode::kLi:
        {
            int32_t imm = setx<16, 32>(instr.imm);
            cpu->setReg(instr.rt, static_cast<word>(imm));
            break;
        }
        case Opcode::kSbit:
        {
            cpu->setReg(instr.rd, 1u << instr.imm5);
            break;
        }
        default:
            cpu->exception = true;
            cpu->cause = ExceptionCause::kIllegalInstruction;
            cpu->exception_value = static_cast<word> (instr.op); 
            break;
    }
    if (!pc_updated) cpu->pc = old_pc + 4;
}

void invalidate_smc (CpuState* cpu, word addr, word size)
{
    if (cpu->bt_cache.empty()) return;

    for (auto it = cpu->bt_cache.begin(); it != cpu->bt_cache.end();)
    {
        word block_start = it->first;
        word block_end = block_start + it->second.block_size;

        if (addr < block_end && addr +size > block_start)
            it = cpu->bt_cache.erase(it);
        else 
            ++it;
    }
}

void handle_syscall(CpuState* cpu)
{
    word num = cpu->syscall_num;

    switch (num)
    {
        case 1:
        {
            std::cout << static_cast<word>(cpu->getReg(0));
            break;
        }
        case 2:
        {
            int32_t v = 0;
            std::cin >> v;
            if (!std::cin)
            {
                std::cerr << "[syscall read_int] input failed\n";
                cpu->setReg(0, static_cast<word>(-1));
                std::cin.clear();
                break;
            }
            cpu->setReg(0, static_cast<word>(v));
            break;
        }
        case 64: 
        {
            word fd = cpu->getReg(0);
            word buf = cpu->getReg(1);
            word count = cpu->getReg(2);

            if (buf > cpu->mem->size() || count > cpu->mem->size() - buf)
            {
                std::cerr << "[syscall write] buffer out of bounds\n";
                cpu->setReg(0, static_cast<word>(-1));
                    return;
            }
            std::ostream& out = (fd == 2) ? std::cerr : std::cout;
                for (word i = 0; i < count; ++i)
                    out << static_cast<char>(cpu->mem->data[buf + i]);

            cpu->setReg(0, count);   // возвращаемое значение = сколько байт вывели
            break;
        }
        case 93:
        {
            cpu->halted = true;
            break;
        }
        default:
        {
            std::cerr << "[syscall] unknown number: " << num << "\n";
            cpu->halted = true;
            break;
        }
    }
}

void handle_exception(CpuState* cpu) 
{
    switch (cpu->cause) 
    {
        case ExceptionCause::kMisalignedAccess:
        case ExceptionCause::kOutOfBounds:
        case ExceptionCause::kIllegalInstruction:
            std::cerr << "Exception: cause=" 
                      << static_cast<int>(cpu->cause)
                      << " value=0x" << std::hex << cpu->exception_value << "\n";
            cpu->halted = true;
            break;
        case ExceptionCause::kNone:
            break;
    }

    cpu->exception = false;
    cpu->cause = ExceptionCause::kNone;
}

BasicBlock& build_bb (CpuState* cpu, word entry_pc)
{
    BasicBlock bb;
    word pc = entry_pc;
    while (true)
    {
        word encoding;
        if (!cpu->mem->read32(pc, encoding)) break;

        Instruction instr = decode (encoding);
        bb.instrs.push_back(instr);
        pc += 4;


        if (instr.op == Opcode::kBeq ||
            instr.op == Opcode::kJ   ||
            instr.op == Opcode::kSyscall) break;

        if (bb.instrs.size() > 256) break;
    }
    auto [it, _] = cpu->bb_cache.emplace(entry_pc, std::move(bb));
    return it->second;
}

void run (CpuState* cpu, asmjit::JitRuntime& rt, bool use_dbt, int threshold)
{
    
    while (!cpu->halted)
    {
        if (use_dbt)
        {
            auto bt_it = cpu->bt_cache.find(cpu->pc);
            if (bt_it != cpu->bt_cache.end())
            {
                bt_it->second.fn(cpu);
                if (cpu->exception) handle_exception(cpu);
                if (cpu->syscall_pending) {
                    handle_syscall(cpu);
                    cpu->syscall_pending = false;
                }
                continue;
            }
        }

        BasicBlock* bb_ptr = nullptr;
        auto bb_it = cpu->bb_cache.find(cpu->pc);
        if (bb_it != cpu->bb_cache.end())
            bb_ptr = &bb_it->second;
        else {
            bb_ptr = &build_bb(cpu, cpu->pc);
            if (cpu->exception) { handle_exception(cpu); continue;}
        }

        if (use_dbt&& bb_ptr->exec_count >= threshold)
        {
            JitFunc fn = generate_block(cpu, rt, bb_ptr->instrs);
            if (fn)
            {
                cpu->bt_cache[cpu->pc] = { fn, (word)(bb_ptr->instrs.size() * 4) };
                fn(cpu);
                if (cpu->exception) handle_exception(cpu);
                if (cpu->syscall_pending) {
                    handle_syscall(cpu);
                    cpu->syscall_pending = false;
                }
                continue;
            }
        }
        bb_ptr->exec_count++;

        for (const Instruction& instr : bb_ptr->instrs) 
        {
            execute(cpu, instr);
            if (cpu->exception || cpu->halted) break;
        }

        if (cpu->exception) handle_exception (cpu);

        if (cpu->syscall_pending)
        {
            handle_syscall(cpu);
            cpu->syscall_pending = false;
        }
        //cpu->dump();
    }   
}

struct RunResult {
    double time_ms;
    bool   ok;
};

RunResult measure_once(const std::string& elf_path, bool use_dbt, int threshold)
{
    Memory mem(1 << 20);
    CpuState cpu;
    cpu.mem = &mem;
    cpu.pc = 0;

    if (!load_elf(mem, cpu, elf_path)) 
        return { 0.0, false };

    asmjit::JitRuntime rt;
    std::streambuf* old = std::cout.rdbuf(nullptr);

    auto t0 = std::chrono::high_resolution_clock::now();
    run(&cpu, rt, use_dbt, threshold);
    auto t1 = std::chrono::high_resolution_clock::now();

    std::cout.rdbuf(old);
    return { std::chrono::duration<double, std::milli>(t1 - t0).count(), true };
}

struct BenchResult 
{
    std::string mode_name;
    int    threshold;
    int    runs;
    double min_ms;
    double median_ms;
    double mean_ms;
    bool   ok;
    double stddev_ms;
};

BenchResult benchmark(const std::string& elf_path,
                     bool use_dbt, int threshold,
                     int warmup, int runs)
{
    BenchResult r;
    r.mode_name = use_dbt ? "DBT" : "Interp";
    r.threshold = threshold;
    r.runs = runs;
    r.ok = false;

    for (int i = 0; i < warmup; ++i) {
        auto w = measure_once(elf_path, use_dbt, threshold);
        if (!w.ok) { return r; }
    }

    std::vector<double> times;
    for (int i = 0; i < runs; ++i) {
        auto t = measure_once(elf_path, use_dbt, threshold);
        if (!t.ok) { return r; }
        times.push_back(t.time_ms);
    }

    std::sort(times.begin(), times.end());
    r.min_ms    = times.front();
    r.median_ms = times[times.size() / 2];
    double sum = 0;
    for (double v : times) sum += v;
    r.mean_ms = sum / times.size();
    r.ok = true;
    return r;
}

#ifndef UNIT_TESTS
int main(int argc, char** argv)
{
    std::string elf_path = "output.elf";
    bool bench_mode = false;
    bool use_dbt = true;
    int  threshold = 10;
    int  runs = 6;
    int  warmup = 2;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--bench")          bench_mode = true;
        else if (arg == "--interp")    use_dbt = false;
        else if (arg == "--dbt")       use_dbt = true;
        else if (arg == "--threshold" && i + 1 < argc) threshold = std::stoi(argv[++i]);
        else if (arg == "--runs"      && i + 1 < argc) runs      = std::stoi(argv[++i]);
        else if (arg == "--warmup"    && i + 1 < argc) warmup    = std::stoi(argv[++i]);
        else if (arg[0] != '-')        elf_path = arg;
    }

    if (!bench_mode)
    {
        Memory mem(1 << 20);           
        CpuState cpu;
        cpu.mem = &mem;
        cpu.pc = 0;

        const char* path = (argc > 1) ? argv[1] : "output.elf";
        if (!load_elf(mem, cpu, path))
        {
            std::cerr << "Failed to load ELF\n";
            return 1;
        }
        asmjit::JitRuntime rt;
        run(&cpu, rt, use_dbt, threshold);
        //cpu.dump(true);
        return 0;
    }

    std::cout << "Benchmark: " << elf_path << "\n";
    std::cout << "Warmup: " << warmup << " runs, " << runs << " timed runs\n\n";

    std::vector<int> thresholds = { 1, 5, 10, 100, 1000, 10000, 25000 };

    auto interp = benchmark(elf_path, false, 0, warmup, runs);
    if (!interp.ok) {
        std::cerr << "Interp benchmark failed\n";
        return 1;
    }

    std::vector<BenchResult> dbt_results;
    for (int t : thresholds) {
        auto r = benchmark(elf_path, true, t, warmup, runs);
        if (!r.ok) {
            std::cerr << "DBT(t=" << t << ") benchmark failed\n";
            return 1;
        }
        dbt_results.push_back(r);
    }

    std::cout << std::left
              << std::setw(20) << "Mode"
              << std::right
              << std::setw(12) << "Min (ms)"
              << std::setw(12) << "Median (ms)"
              << std::setw(12) << "Mean (ms)"
              << std::setw(12) << "Speedup"
              << "\n";
    std::cout << std::string(68, '-') << "\n";

    auto print_row = [&](const BenchResult& r) {
        double speedup = interp.median_ms / r.median_ms;
        std::string name = r.mode_name;
        if (r.mode_name == "DBT") name += "(t=" + std::to_string(r.threshold) + ")";
        std::cout << std::left << std::setw(20) << name
                  << std::right
                  << std::setw(12) << std::fixed << std::setprecision(2) << r.min_ms
                  << std::setw(12) << r.median_ms
                  << std::setw(12) << r.mean_ms
                  << std::setw(11) << speedup << "x"
                  << "\n";
    };

    print_row(interp);
    for (auto& r : dbt_results) print_row(r);

    std::cout << "\n";
    return 0;
}
#endif
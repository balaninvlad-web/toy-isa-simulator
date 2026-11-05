// generate_block.hpp
#pragma once
#include "main_types.hpp"
#include <asmjit/x86.h>

JitFunc generate_block(CpuState* cpu, asmjit::JitRuntime& rt,
                       const std::vector<Instruction>& block);
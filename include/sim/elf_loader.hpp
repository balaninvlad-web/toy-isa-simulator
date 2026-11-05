#pragma once
#include "main_types.hpp"
#include <filesystem>

namespace ELFIO
{
    class elfio;
    class segment;
}
class ELFloader
{
private:
    void validateElf(const ELFIO::elfio& elf);
    void loadSegment(const ELFIO::segment& seg, Memory& memory);
public:
    void loadProgram(const std::filesystem::path& path, Memory& memory, CpuState& state);
};

bool load_elf(Memory& mem, CpuState& cpu, const std::string& path);
#include "elf_loader.hpp"
#include "elfio/elfio.hpp"
#include <stdexcept>
#include <iostream>

constexpr std::uint16_t EM_TOY = 0xFF00;

bool load_elf(Memory& mem, CpuState& cpu, const std::string& path) 
{
    try 
    {
        ELFloader loader;
        loader.loadProgram(path, mem, cpu);
        return true;
    } catch (const std::exception& e) {
        std::cerr << "ELF error: " << e.what() << "\n";
        return false;
    }
}

void ELFloader::validateElf(const ELFIO::elfio &elf)
{
    if (elf.get_class() != ELFIO::ELFCLASS32)
    {
        throw std::runtime_error("Only ELF32 supported");
    }
    if (elf.get_encoding() != ELFIO::ELFDATA2LSB)
    {
        throw std::runtime_error("Only little-endian supported");
    }

    if (elf.get_machine() != EM_TOY)
    {
        throw std::runtime_error("Not ToyISA ELF file");
    }
}

void ELFloader::loadSegment(const ELFIO::segment &seg, Memory &memory)
{
    const auto vaddr   = seg.get_virtual_address();
    const auto filesz  = seg.get_file_size();
    const auto memsz   = seg.get_memory_size();

    if (memsz < filesz)
    {
        throw std::runtime_error("Segment sizes invalid");
    }
    if (vaddr + memsz > memory.size())
    { 
        throw std::runtime_error("Segment exceeds memory");
    }

    const auto addr = static_cast<word>(vaddr);

    if (filesz > 0) 
    {
        const char* data = seg.get_data();
        if (!data) throw std::runtime_error("Missing segment data");
        std::memcpy(memory.data.data() + addr, data, filesz);
    }

    if (memsz > filesz)//обнуляю .bss
    { 
        std::memset(memory.data.data() + addr + filesz, 0, memsz - filesz);
    }
}

void ELFloader::loadProgram(const std::filesystem::path &path, Memory &memory, CpuState &state)
{
    ELFIO::elfio elf;
    if (!elf.load(path.string())) 
        throw std::runtime_error("Cannot load ELF: " + path.string());

    validateElf(elf);

    state.pc = static_cast<word>(elf.get_entry());

    for (const auto& seg : elf.segments)
    {
        if (seg->get_type() == ELFIO::PT_LOAD)
        {
            loadSegment(*seg, memory);
        }
    }
}

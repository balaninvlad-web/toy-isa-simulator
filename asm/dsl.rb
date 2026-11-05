class Symbol 
    def to_reg = to_s[1..].to_i  # r5->"r5"->"5"->5
end

class Integer
    def to_reg = self
end

ISA = {
  li:      { opc: 0b010001, args: %i[rt imm],                   bits: { rt: [16,5], imm: [0,16] } },
  addi:    { opc: 0b111001, args: %i[rt rs imm],                bits: { rs: [21,5], rt: [16,5], imm: [0,16] } },
  add:     { opc: 0b000000, fn: 0b110010, args: %i[rd rs rt],   bits: { rs: [21,5], rt: [16,5], rd: [11,5], fn: [0,6] } },
  beq:     { opc: 0b111111, args: %i[rs rt offset],             bits: { rs: [21,5], rt: [16,5], offset: [0,16] } },
  j:       { opc: 0b101111, args: %i[index],                    bits: { index: [0,26] } },
  ld:      { opc: 0b010011, args: %i[rt base imm],              bits: { base: [21,5], rt: [16,5], imm: [0,14] } },
  st:      { opc: 0b011110, args: %i[rt base imm],              bits: { base: [21,5], rt: [16,5], imm: [0,14] } },
  stp:     { opc: 0b001110, args: %i[rt1 rt2 base imm],         bits: { base: [21,5], rt1: [16,5], rt2: [11,5], imm: [0,11] } },
  movz:    { opc: 0b000000, fn: 0b011100, args: %i[rd rs rt],   bits: { rs: [21,5], rt: [16,5], rd: [11,5], fn: [0,6] } },
  movn:    { opc: 0b000000, fn: 0b000101, args: %i[rd rs rt],   bits: { rs: [21,5], rt: [16,5], rd: [11,5], fn: [0,6] } },
  selc:    { opc: 0b000000, fn: 0b010000, args: %i[rd rs1 rs2], bits: { rd: [21,5], rs1: [16,5], rs2: [11,5], fn: [0,6] } },
  cls:     { opc: 0b000000, fn: 0b110011, args: %i[rd rs],      bits: { rd: [21,5], rs: [16,5], fn: [0,6] } },
  ssat:    { opc: 0b111100, args: %i[rd rs imm5],               bits: { rd: [21,5], rs: [16,5], imm5: [11,5] } },
  sbit:    { opc: 0b010010, args: %i[rd rs imm5],               bits: { rd: [21,5], rs: [16,5], imm5: [11,5] } },
  syscall: { opc: 0b000000, fn: 0b011101, args: [],             bits: { fn: [0,6] } },
}.freeze

EM_TOY = 0xFF00

module Encoder
    private

    def mask(width) = (1 << width) - 1

    def emit(word)
        @code << (word & 0xFFFFFFFF)
        @pc += 4
    end

    def encode(name, *args)
        spec = ISA[name] or raise "unknown instruction: #{name}"
        unless args.size == spec[:args].size
            raise ArgumentError, "#{name}: expected #{spec[:args].size} args, got #{args.size}"
        end

        values = spec[:args].zip(args).to_h
        word   = spec[:opc] << 26

        spec[:bits].each do |field, (pos, width)|
            value = case field
                    when :fn    then spec[:fn]
                    when :imm    then values[:imm]  & mask(width)
                    when :imm5   then values[:imm5] & mask(width)
                    when :offset then resolve_offset(values[:offset], width)
                    when :index  then resolve_index(values[:index], width)
                    else              values[field].to_reg & mask(width)
                    end
            word |= (value & mask(width)) << pos
        end
        emit(word)
    end

    def resolve_offset(target, width)
        insn_pc = @pc
        case target
        when Integer        then ((target - insn_pc) / 4) & mask(width)
        when Symbol, String
            sym = target.to_sym
            if @labels.key?(sym)
                ((@labels[sym] - insn_pc) / 4) & mask(width)
            else
                @fixups << { pc: insn_pc, name: sym, kind: :offset, width: width }
                0
            end
        else raise ArgumentError, "bad offset: #{target.inspect}"
        end
    end

    def resolve_index(target, width)
        insn_pc = @pc
        case target 
        when Integer        then (target >> 2) & mask(width)
        when Symbol, String
            sym = target.to_sym
            if @labels.key?(sym)
                (@labels[sym] >> 2) & mask(width)
            else
                @fixups << {pc: insn_pc, name: sym, kind: :index, width: width}
                0
            end
        else raise ArgumentError, "bad index: #{target.inspect}"
        end
    end
end

class Assembler
    include Encoder

    (0..31).each do |i|
        define_method("r#{i}") { i }
        define_method("x#{i}") { i }
    end

    ISA.each_key do |name|
        define_method(name) { |*args| encode(name, *args)}
    end

    def initialize
        @code   = []
        @labels = {}
        @fixups = []
        @pc     = 0
    end

    alias mov li

    def add(rd, a, b = nil)
        b.nil? ? encode(:addi, rd, rd, a) : encode(:add, rd, a, b)
    end

    def hlt
        encode(:li, 8, 93)
        encode(:syscall)
    end
  
    def label(name)
        @labels[name.to_sym] = @pc
    end
  
    def method_missing(name, *args)
        return super unless ISA.key?(name)
        encode(name, *args)
    end
  
    def respond_to_missing?(name, include_private = false)
        ISA.key?(name) || super
    end
  
    def save_elf(path)
        @fixups.each do |f|
        addr = @labels[f[:name]] or raise "undefined label: #{f[:name]}"
        idx  = f[:pc] / 4
        val  = case f[:kind]
               when :offset then ((addr - f[:pc]) / 4) & ((1 << f[:width]) - 1)
               when :index  then (addr >> 2)         & ((1 << f[:width]) - 1)
               end
        @code[idx] |= val
        end

        code = @code.pack("V*")
        e_ident     = "\x7FELF".b + "\x01\x01\x01" + ("\x00" * 9)
        e_type      = [0x02].pack("v")         
        e_machine   = [EM_TOY].pack("v")       
        e_version   = [1].pack("V")
        e_entry     = [0].pack("V")            # старт с 0
        e_phoff     = [52].pack("V")           # Phdr сразу после EHdr
        e_shoff     = [0].pack("V")
        e_flags     = [0].pack("V")
        e_ehsize    = [52].pack("v")
        e_phentsize = [32].pack("v")
        e_phnum     = [1].pack("v")            # один PT_LOAD
        e_shentsize = [40].pack("v")
        e_shnum     = [0].pack("v")
        e_shstrndx  = [0].pack("v")

        ehdr = e_ident + e_type + e_machine + e_version + e_entry +
               e_phoff + e_shoff + e_flags + e_ehsize + e_phentsize +
               e_phnum + e_shentsize + e_shnum + e_shstrndx
        raise "EHdr size: #{ehdr.bytesize}" unless ehdr.bytesize == 52

        p_type   = [1].pack("V")               # PT_LOAD
        p_offset = [52 + 32].pack("V")         # данные после заголовков
        p_vaddr  = [0].pack("V")               # в память по адресу 0
        p_paddr  = [0].pack("V")
        p_filesz = [code.bytesize].pack("V")
        p_memsz  = [code.bytesize].pack("V")   # нет .bss
        p_flags  = [0x5].pack("V")             # PF_R | PF_X
        p_align  = [4].pack("V")

        phdr = p_type + p_offset + p_vaddr + p_paddr + p_filesz +
               p_memsz + p_flags + p_align
        raise "Phdr size: #{phdr.bytesize}" unless phdr.bytesize == 32

        File.binwrite(path, ehdr + phdr + code)
        puts "→ #{path}: ELF32, #{@code.length} инструкций, #{ehdr.bytesize + phdr.bytesize + code.bytesize} байт"
    end

end

def prog(path: "output.elf", &block)
    asm = Assembler.new
    asm.instance_eval(&block)
    asm.save_elf(path)
end



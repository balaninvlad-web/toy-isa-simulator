# bench2.rb
require_relative 'dsl'

BLOCKS = 50
SIZE   = 10
OUTER  = 20_000

prog(path: "bench2.elf") do
  li 31, 0                 # zero
  li 20, OUTER             # outer counter
  # регистры r1..r15 — обнуляем (больше не нужно)
  (1..15).each { |r| li r, 0 }

  label :outer
  beq 20, 31, :done

  # ─── 50 forward jumps — каждый создаёт свой BB ───
  BLOCKS.times do |i|
    j     :"b#{i}"
    label :"r#{i}"
  end

  addi 20, 20, -1
  j :outer

  # ─── 50 блоков по 10 ADDI + J назад ───
  BLOCKS.times do |i|
    label :"b#{i}"
    SIZE.times { addi (i % 15 + 1), (i % 15 + 1), 1 }
    j :"r#{i}"
  end

  label :done
  li 8, 93
  syscall
end
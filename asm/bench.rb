# bench.rb
require_relative 'dsl'

prog(path: "bench.elf") do
  li   r31, 0
  li   r3, 10000            # outer loop = 10000
  li   r1, 0
  li   r2, 1
  li   r5, 100              # inner loop = 100

  label :outer
  beq  r3, r31, :done

  label :inner
  add  r1, r1, r2
  addi r5, r5, -1
  beq  r5, r31, :inner_done
  j    :inner

  label :inner_done
  li   r5, 100              # reset inner counter
  addi r3, r3, -1
  j    :outer

  label :done

  li   r8, 93
  syscall
end
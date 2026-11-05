require_relative 'dsl'

prog do           
  li   r31, 0

  li   r8, 2
  syscall
  movz r3, r0, r31  

  li   r1, 0             
  li   r2, 1                      

  label :loop
  beq  r3, r31, :result
  add  r4, r1, r2        # t = a + b
  movz r1, r2, r31       # a = b
  movz r2, r4, r31       # b = t
  addi r3, r3, -1        # n -= 1
  j    :loop

  label :result
  movz r0, r1, r31
  li r8, 1
  syscall

  li   r8, 93            # exit
  syscall
end
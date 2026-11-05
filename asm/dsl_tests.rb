require_relative 'dsl'


prog do
    #_______consts________
    li  31, 0       #zero registr
    li  20, 0x300   #buffer state
    li  21, 49      #'1'
    li  22, 48      #'0'
    #______________________

    #LI
    li    1, 42
    li    2, 42
    beq   1, 2, :t00_ok
    st    22, 20, 0
    j     :t00_end
    label :t00_ok
    st    21, 20, 0
    label :t00_end

    #ADD
    li    1, 5
    li    2, 7
    add   3, 1, 2
    li    4, 12
    beq   3, 4, :t01_ok
    st    22, 20, 4
    j     :t01_end
    label :t01_ok
    st    21, 20, 4
    label :t01_end

    #ADDI
    li    1, 5
    addi  2, 1, 3
    li    3, 8
    beq   2, 3, :t02_ok
    st    22, 20, 8
    j     :t02_end
    label :t02_ok
    st    21, 20, 8
    label :t02_end

    #ADDI (-)
    li    1, 5
    addi  2, 1, -3
    li    3, 2
    beq   2, 3, :t03_ok
    st    22, 20, 12
    j     :t03_end
    label :t03_ok
    st    21, 20, 12
    label :t03_end

    #BEQ(true)
    li    1, 5
    li    2, 5
    beq   1, 2, :t04_ok
    st    22, 20, 16
    j     :t04_end
    label :t04_ok
    st    21, 20, 16
    label :t04_end

    #BEQ(false)
    li    1, 5
    li    2, 6
    beq   1, 2, :t05_fail   
    st    21, 20, 20      
    j     :t05_end
    label :t05_fail
    st    22, 20, 20
    label :t05_end

    #J
    j     :t06_ok
    st    22, 20, 24
    j     :t06_end
    label :t06_ok
    st    21, 20, 24
    label :t06_end

    #ST + LD
    li   1, 42
    li   2, 0x400
    st   1, 2, 0           # mem[0x400] = 42
    ld   3, 2, 0           # r3 = mem[0x400]
    li   4, 42
    beq  3, 4, :t07_ok
    st   22, 20, 28
    j    :t07_end
    label :t07_ok
    st   21, 20, 28
    label :t07_end

    #STP (2 words)
    li   1, 100
    li   2, 200
    li   3, 0x420
    stp  1, 2, r3, 0       # mem[0x420]=100, mem[0x424]=200
    ld   4, 3, 0           # r4 = 100
    ld   5, 3, 4           # r5 = 200
    li   6, 100
    li   7, 200
    beq  4, 6, :t08_first
    j    :t08_fail
    label :t08_first
    beq  5, 7, :t08_ok
    j    :t08_fail
    label :t08_fail
    st   22, 20, 32
    j    :t08_end
    label :t08_ok
    st   21, 20, 32
    label :t08_end

    #MOVZ — rt == 0
    li   1, 42
    li   2, 0
    movz 3, 1, 2           # r3 = 42
    li   4, 42
    beq  3, 4, :t09_ok
    st   22, 20, 36
    j    :t09_end
    label :t09_ok
    st   21, 20, 36
    label :t09_end

    #MOVZ — rt != 0
    li   1, 42
    li   2, 5
    li   3, 0
    movz 3, 1, 2           # r3 остаётся 0
    beq  3, 31, :t10_ok    # r3 == 0 (r31 = 0)
    st   22, 20, 40
    j    :t10_end
    label :t10_ok
    st   21, 20, 40
    label :t10_end

    #MOVN — rt != 0
    li   1, 42
    li   2, 5
    movn 3, 1, 2           # r3 = 42
    li   4, 42
    beq  3, 4, :t11_ok
    st   22, 20, 44
    j    :t11_end
    label :t11_ok
    st   21, 20, 44
    label :t11_end

    #CLS — все единицы -> 32
    li   1, 0xFFFF         # sign-extend → 0xFFFFFFFF
    cls  2, 1              # r2 = 32
    li   3, 32
    beq  2, 3, :t12_ok
    st   22, 20, 48
    j    :t12_end
    label :t12_ok
    st   21, 20, 48
    label :t12_end

    #SSAT — 1000 -> 127
    li   1, 1000
    ssat 2, 1, 8           # r2 = 127
    li   3, 127
    beq  2, 3, :t13_ok
    st   22, 20, 52
    j    :t13_end
    label :t13_ok
    st   21, 20, 52
    label :t13_end

    #SELC — max(5, 3) = 5
    li   1, 5
    li   2, 3
    selc 3, 1, 2           # r3 = 5
    li   4, 5
    beq  3, 4, :t14_ok
    st   22, 20, 56
    j    :t14_end
    label :t14_ok
    st   21, 20, 56
    label :t14_end

    #SBIT — 0 -> r1 = 1
    sbit 1, 31, 0          # r1 = 1 << 0 = 1
    li   2, 1
    beq  1, 2, :t15_ok
    st   22, 20, 60
    j    :t15_end
    label :t15_ok
    st   21, 20, 60
    label :t15_end

    # out 16 symbols
    li   10, 16            # counter = 16
    add  1, 20, 31         # r1 = buf (0x300 + r31=0)
    li   2, 1              # count = 1 байт
    li   0, 1              # fd = stdout

    label :out_loop
    li   8, 64             # syscall write
    syscall
    addi 1, 1, 4           # buf += 4
    addi 10, 10, -1        # counter--
    beq  10, 31, :out_done # counter == 0?
    j    :out_loop

    label :out_done
    li   8, 93             # syscall exit
    syscall
end
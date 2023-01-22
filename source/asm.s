.arm
.fpu vfp
.align(4);
.section .rodata

.global customBreak
.type customBreak, %function
customBreak:
    SVC 0x3C
    BX LR


.global msbtconstfunc
.type msbtconstfunc, %function
msbtconstfunc:
    LDR R0, [R0]
    STR R0, [R4,#0x30]
    push {r0-r4}
    mov r0, R4
    bl getmsbtptrfromobj
    pop {r0-r4}
    ldr r1, =g_msbtconstret
    ldr pc, [r1]
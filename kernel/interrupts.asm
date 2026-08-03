BITS 32

global keyboard_stub
global timer_stub
extern keyboard_handler
extern timer_handler

keyboard_stub:
    pusha
    call keyboard_handler
    popa
    iretd

timer_stub:
    pusha
    call timer_handler
    popa
    iretd
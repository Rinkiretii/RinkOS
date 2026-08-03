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

    global task_switch

task_switch:
    push ebp
    push ebx
    push esi
    push edi

    mov eax, [esp+20]      ; old_esp_store (accounting for the 4 pushes above)
    mov [eax], esp         ; save current esp into *old_esp_store

    mov esp, [esp+24]      ; load new task's esp

    pop edi
    pop esi
    pop ebx
    pop ebp
    ret                    ; "returns" into the new task's entry point
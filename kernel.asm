; kernel.asm
[bits 32]

start:
    cli           
    mov ax, 0x10 
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

  
    mov si, welcome_msg
    call print_string

   
main_loop:
    mov si, prompt_msg
    call print_string
    call get_input
    cmp byte [input_buffer], 0
    je main_loop  


    mov si, input_buffer
    cmp byte [si], 'h'
    je handle_help
    cmp byte [si], 'r'
    je handle_reboot

   
    mov si, unknown_cmd
    call print_string
    jmp main_loop

handle_help:
    mov si, help_msg
    call print_string
    jmp main_loop

handle_reboot:
  
    mov al, 0xFE
    out 0x64, al

unknown_cmd db "Unknown command.", 0

get_input:
 
    mov di, input_buffer
    mov cx, 256        
    xor ax, ax        
clear_buffer:
    stosb              
    loop clear_buffer

  
    mov byte [input_buffer], 'h'
    ret

print_string:
   
    .print_loop:
        lodsb          
        or al, al     
        jz .done
        mov ah, 0x0E  
        int 0x10
        jmp .print_loop
    .done:
        ret

input_buffer db 256 dup(0) 
welcome_msg db "Welcome to RinkOS!", 0
prompt_msg db "RinkOS> ", 0
help_msg db "Commands: h (help), r (reboot)", 0

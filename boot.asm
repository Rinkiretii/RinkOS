; boot.asm
[org 0x7c00]       
bits 16

start:
   
    mov ah, 0x00
    mov al, 0x03    
    int 0x10

 
    mov si, os_name
    call print_string

    
    call load_kernel

   
    cli
    hlt

print_string:
   
    lodsb          
    or al, al      
    jz .done
    mov ah, 0x0E    
    int 0x10
    jmp print_string
.done:
    ret

load_kernel:
   
    mov bx, 0x1000  
    mov ah, 0x02   
    mov al, 1     
    mov ch, 0    
    mov cl, 2  
    mov dh, 0  
    int 0x13
    jmp 0x1000    
    ret

os_name db "RinkOS v0.1", 0

times 510-($-$$) db 0
dw 0xAA55        

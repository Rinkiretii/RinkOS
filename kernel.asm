; kernel.asm
[bits 32]            ; Указываем, что используем 32-битный режим

start:
    cli             ; Отключаем прерывания
    mov ax, 0x10    ; Устанавливаем сегмент данных
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ; Вывод приветственного сообщения
    mov si, welcome_msg
    call print_string

    ; Базовый текстовый интерфейс
main_loop:
    mov si, prompt_msg
    call print_string
    call get_input
    cmp byte [input_buffer], 0
    je main_loop    ; Если ввода нет, продолжаем цикл

    ; Проверка команды
    mov si, input_buffer
    cmp byte [si], 'h'
    je handle_help
    cmp byte [si], 'r'
    je handle_reboot

    ; Неизвестная команда
    mov si, unknown_cmd
    call print_string
    jmp main_loop

handle_help:
    mov si, help_msg
    call print_string
    jmp main_loop

handle_reboot:
    ; Простейшая перезагрузка
    mov al, 0xFE
    out 0x64, al

unknown_cmd db "Unknown command.", 0

get_input:
    ; Заглушка для получения пользовательского ввода
    ; Очистка буфера
    mov di, input_buffer
    mov cx, 256          ; Длина буфера
    xor ax, ax           ; Обнуляем
clear_buffer:
    stosb                ; Записываем ноль в буфер
    loop clear_buffer

    ; Ввод
    ; Здесь просто заглушка для демонстрации
    mov byte [input_buffer], 'h' ; Просто для примера, чтобы была команда 'h'
    ret

print_string:
    ; Функция для вывода строки на экран
    ; В reg si находится указатель на строку
    .print_loop:
        lodsb           ; Загружаем следующий символ в AL
        or al, al       ; Проверяем конец строки (нулевой байт)
        jz .done
        mov ah, 0x0E    ; BIOS: Вывод символа
        int 0x10
        jmp .print_loop
    .done:
        ret

input_buffer db 256 dup(0)   ; Буфер для ввода (256 байт)
welcome_msg db "Welcome to RinkOS!", 0
prompt_msg db "RinkOS> ", 0
help_msg db "Commands: h (help), r (reboot)", 0

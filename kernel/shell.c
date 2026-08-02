#include <stdint.h>
#include "scr/keyboard.h"
#include "scr/mm.h"
#include "scr/mmap.h"

extern void kprint(const char *);
extern void vga_clear(void);
extern void vga_enable_cursor(void);

#define LINE_BUF_SIZE 128

static int str_eq(const char *a, const char *b)
{
    while (*a && *b) {
        if (*a != *b) return 0;
        a++; b++;
    }
    return *a == *b;
}

static void cmd_help(void)
{
    kprint("Available commands:\n");
    kprint("  help    - show this list\n");
    kprint("  clear   - clear the screen\n");
    kprint("  echo    - print text back, e.g. echo hello\n");
    kprint("  info    - OS and system information\n");
}

static void cmd_echo(const char *args)
{
    /* skip the leading space, if present */
    if (*args == ' ') args++;
    kprint(args);
    kprint("\n");
}

static void cmd_info(void)
{
    kprint("RinkOS 0.06\n");
}

static void run_command(char *line)
{
    if (line[0] == '\0') {
        return; /* empty line, nothing to do */
    }

    if (str_eq(line, "help")) {
        cmd_help();
    } else if (str_eq(line, "clear")) {
        vga_clear();
        vga_enable_cursor();
    } else if (line[0] == 'e' && line[1] == 'c' && line[2] == 'h' &&
               line[3] == 'o' && (line[4] == ' ' || line[4] == '\0')) {
        cmd_echo(line + 4);
    } else if (str_eq(line, "info")) {
        cmd_info();
    } else {
        kprint(line);
        kprint(": command not found\n");
    }
}

void shell_run(void)
{
    char *line = (char *)kmalloc(LINE_BUF_SIZE);
    int pos = 0;

    kprint("> ");

    for (;;) {
        char c = keyboard_getchar();

        if (c == '\n') {
            kprint("\n");
            line[pos] = '\0';
            run_command(line);
            pos = 0;
            kprint("> ");
        } else if (c == '\b') {
            if (pos > 0) {
                pos--;
                kprint("\b");   /* your vga_putchar already erases on \b */
            }
        } else if (pos < LINE_BUF_SIZE - 1) {
            line[pos++] = c;
            char str[2] = {c, '\0'};
            kprint(str);
        }
    }
}
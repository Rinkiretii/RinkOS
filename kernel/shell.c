#include <stdint.h>
#include "scr/keyboard.h"
#include "scr/mm.h"
#include "scr/mmap.h"
#include "scr/shell.h"

extern void kprint(const char *);
extern void vga_clear(void);
extern void vga_enable_cursor(void);
extern void kmalloc_stats(size_t *total, size_t *used, size_t *free_bytes);
extern void reboot(void);
extern void mmap_dump(void);
extern uint32_t timer_get_seconds(void);
extern char *system_version;


#define LINE_BUF_SIZE 128

static int str_eq(const char *a, const char *b)
{
    while (*a && *b) {
        if (*a != *b) return 0;
        a++; b++;
    }
    return *a == *b;
}

static void kprint_uint(uint32_t val)
{
    char buf[11];
    int i = 10;
    buf[i--] = '\0';

    if (val == 0) {
        buf[i--] = '0';
    } else {
        while (val > 0) {
            buf[i--] = '0' + (val % 10);
            val /= 10;
        }
    }
    kprint(&buf[i + 1]);
}

static void cmd_uptime(void)
{
    kprint("Uptime: ");
    kprint_uint(timer_get_seconds());
    kprint(" second\n");
}

static void cmd_help(void)
{
    kprint("Available commands:\n");
    kprint("  help    - show this list\n");
    kprint("  clear   - clear the screen\n");
    kprint("  echo    - print text back, e.g. echo hello\n");
    kprint("  info    - OS and system information\n");
    kprint("  meminfo - show heap usage\n");
    kprint("  reboot  - restart\n");
    kprint("  shutdown- power off\n");
    kprint("  uptime  - show system uptime in seconds\n");
}

static void cmd_echo(const char *args)
{
    /* skip the leading space, if present */
    if (*args == ' ') args++;
    kprint(args);
    kprint("\n");
}

static void cmd_meminfo(void)
{
    size_t total, used, free_bytes;
    kmalloc_stats(&total, &used, &free_bytes);

    kprint("Heap total: ");
    kprint_uint((uint32_t)total);
    kprint(" bytes\n");

    kprint("Heap used:  ");
    kprint_uint((uint32_t)used);
    kprint(" bytes\n");

    kprint("Heap free:  ");
    kprint_uint((uint32_t)free_bytes);
    kprint(" bytes\n");
}

static void debug(void)
{
    kprint("Debug info:\n");
    kprint(system_version);
    kprint("\n");
    cmd_meminfo();
    kprint("Uptime: ");
    kprint_uint(timer_get_seconds());
    kprint(" seconds\n");
    mmap_dump();
}

static void cmd_info(void)
{
    kprint(system_version);
    kprint("\n");
    kprint("Uptime: ");
    kprint_uint(timer_get_seconds());
    kprint(" seconds\n");
    cmd_meminfo();
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
    } else if (str_eq(line, "meminfo")) {
        cmd_meminfo();
    } else if (str_eq(line, "reboot")) {
        reboot();
    } else  if (str_eq(line, "shutdown")) {
        reboot();
    } else  if (str_eq(line, "uptime")) {
        cmd_uptime();
    } else if (str_eq(line, "debug")) {
        debug();
    } else {
        kprint("Unknown command: ");
        kprint(line);
        kprint("\n");
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
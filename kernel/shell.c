#include <stdint.h>
#include "scr/keyboard.h"
#include "scr/mm.h"
#include "scr/mmap.h"
#include "scr/shell.h"
#include "scr/timer.h"
#include "scr/task.h"
#include "scr/fs.h"

extern void kprint(const char *);
extern void vga_clear(void);
extern void vga_enable_cursor(void);
extern void kmalloc_stats(size_t *total, size_t *used, size_t *free_bytes);
extern void reboot(void);
extern void shutdown(void);
extern void mmap_dump(void);
extern uint32_t timer_get_seconds(void);
extern char *system_version;
extern uint32_t counter;
extern void kprint_uint();

#define LINE_BUF_SIZE 258
#define HISTORY_SIZE 16

static char history[HISTORY_SIZE][LINE_BUF_SIZE];
static int history_count = 0;

static int str_eq(const char *a, const char *b)
{
    while (*a && *b) {
        if (*a != *b) return 0;
        a++; b++;
    }
    return *a == *b;
}

static void error_command_no_work_C(void) {
    kprint(" 001 this command in develop, sorry.");
}

static void cmd_ls(void)
{
    char cwd[FS_NAME_LEN];
    fs_getcwd(cwd, sizeof(cwd));
    kprint("Contents of ");
    kprint(cwd);
    kprint(":\n");
    fs_list();
}

static void cmd_pwd(void)
{
    char cwd[FS_NAME_LEN];
    fs_getcwd(cwd, sizeof(cwd));
    kprint(cwd);
    kprint("\n");
}

static void cmd_mkdir(const char *args)
{
    if (*args == ' ') args++;
    if (*args == '\0') {
        kprint("usage: mkdir <path>\n");
        return;
    }

    int result = fs_mkdir(args);
    if (result < 0) {
        kprint("mkdir failed (already exists or path invalid)\n");
    } else {
        kprint("Created directory ");
        kprint(args);
        kprint("\n");
    }
}

static void cmd_cd(const char *args)
{
    if (*args == ' ') args++;
    if (*args == '\0') {
        kprint("usage: cd <path>\n");
        return;
    }

    int result = fs_chdir(args);
    if (result < 0) {
        kprint("cd: no such directory: ");
        kprint(args);
        kprint("\n");
    }
}

static void str_copy_local(char *dst, const char *src, int max_len)
{
    int i = 0;
    while (src[i] && i < max_len - 1) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

static void cmd_write(const char *args)
{
    if (*args == ' ') args++;

    /* parse: <filename> <rest of line as file content> */
    char filename[FS_NAME_LEN];
    int i = 0;
    while (args[i] && args[i] != ' ' && i < FS_NAME_LEN - 1) {
        filename[i] = args[i];
        i++;
    }
    filename[i] = '\0';

    if (i == 0) {
        kprint("usage: write <filename> <text>\n");
        return;
    }

    const char *content = args + i;
    if (*content == ' ') content++;

    int len = 0;
    while (content[len]) len++;

    int result = fs_write(filename, (const uint8_t *)content, (uint32_t)len);
    if (result < 0) {
        kprint("write failed (file table full?)\n");
    } else {
        kprint("Write ");
        kprint_uint((uint32_t)len);
        kprint(" bytes to ");
        kprint(filename);
        kprint("\n");
    }
}

static void cmd_cat(const char *args)
{
    if (*args == ' ') args++;

    if (*args == '\0') {
        kprint("usage: cat <filename>\n");
        return;
    }

    static uint8_t buf[4096]; /* matches FS_MAX_FILE_SECTORS * 512 */
    uint32_t out_size = 0;

    int result = fs_read(args, buf, sizeof(buf) - 1, &out_size);
    if (result < 0) {
        kprint(args);
        kprint(": not found\n");
        return;
    }

    buf[out_size] = '\0';
    kprint((const char *)buf);
    kprint("\n");
}

static void cmd_mv(const char *args)
{
    if (*args == ' ') args++;
    char old_name[FS_NAME_LEN];
    int i = 0;
    while (args[i] && args[i] != ' ' && i < FS_NAME_LEN - 1) { old_name[i] = args[i]; i++; }
    old_name[i] = '\0';

    const char *rest = args + i;
    if (*rest == ' ') rest++;

    if (i == 0 || *rest == '\0') {
        kprint("usage: mv <old> <new>\n");
        return;
    }

    int result = fs_rename(old_name, rest);
    if (result < 0) {
        kprint("mv failed (not found or target exists)\n");
    } else {
        kprint("Renamed ");
        kprint(old_name);
        kprint(" to ");
        kprint(rest);
        kprint("\n");
    }
}

void kprint_uint(uint32_t val)
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

static void cmd_delete(const char *args)
{
    if (*args == ' ') args++;
    if (*args == '\0') {
        kprint("usage: delete <filename>\n");
        return;
    }

    int result = fs_delete(args);
    if (result < 0) {
        kprint(args);
        kprint(": not found\n");
    } else {
        kprint("Deleted ");
        kprint(args);
        kprint("\n");
    }
}

static void cmd_append(const char *args)
{
    if (*args == ' ') args++;

    char filename[FS_NAME_LEN];
    int i = 0;
    while (args[i] && args[i] != ' ' && i < FS_NAME_LEN - 1) {
        filename[i] = args[i];
        i++;
    }
    filename[i] = '\0';

    if (i == 0) {
        kprint("usage: append <filename> <text>\n");
        return;
    }

    const char *content = args + i;
    if (*content == ' ') content++;

    int len = 0;
    while (content[len]) len++;

    int result = fs_append(filename, (const uint8_t *)content, (uint32_t)len);
    if (result < 0) {
        kprint("append failed (file too large or table full)\n");
    } else {
        kprint("Appended ");
        kprint_uint((uint32_t)len);
        kprint(" bytes to ");
        kprint(filename);
        kprint("\n");
    }
}


static void cmd_stat(const char *args)
{
    if (*args == ' ') args++;
    if (*args == '\0') {
        kprint("usage: stat <filename>\n");
        return;
    }

    uint32_t size;
    int result = fs_stat(args, &size);
    if (result < 0) {
        kprint(args);
        kprint(": not found\n");
        return;
    }

    kprint(args);
    kprint(": ");
    kprint_uint(size);
    kprint(" bytes\n");
}

static void cmd_top(void)
{
    kprint("Tasks:\n");
    task_list();
}

static void cmd_help(void)
{
    kprint("Available commands:\n");
    kprint(" help             - show this list\n");
    kprint(" clear            - clear the screen\n");
    kprint(" echo             - print text back, e.g. echo hello\n");
    kprint(" info             - OS and system information\n");
    kprint(" meminfo          - show heap usage\n");
    kprint(" reboot           - restart\n");
//    kprint(" shutdown         - power off\n");
//    kprint(" set_max_task     - set max task");
    kprint(" uptime           - show system uptime in seconds\n");
    kprint(" history          - show recent commands\n");
    kprint(" ls               - list files\n");
    kprint(" write <f> <txt>  - write text to a file\n");
    kprint(" cat <f>          - print a file's contents\n");
    kprint(" delete <f>       - delete a file\n");
    kprint(" stat <f>         - show file size\n");
    kprint(" mv <old> <new>   - rename a file\n");
    kprint(" append <f> <txt> - append text to a file\n");
    kprint(" mkdir <path>     - create a directory\n");
    kprint(" cd <path>        - change directory (.. goes up, / is root)\n");
    kprint(" pwd              - show current directory\n");
    kprint(" top              - show running tasks\n");
}

static void cmd_echo(const char *args)
{
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


static void history_add(const char *line)
{
    if (line[0] == '\0') return;
    for (int i = 0; i < HISTORY_SIZE - 1; i++) {
        str_copy_local(history[i], history[i + 1], LINE_BUF_SIZE);
    }
    str_copy_local(history[HISTORY_SIZE - 1], line, LINE_BUF_SIZE);
    if (history_count < HISTORY_SIZE) history_count++;
}

static void cmd_history(void)
{
    int start = HISTORY_SIZE - history_count;
    for (int i = start; i < HISTORY_SIZE; i++) {
        kprint("  ");
        kprint(history[i]);
        kprint("\n");
    }
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

    history_add(line);

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
        shutdown();
    } else  if (str_eq(line, "uptime")) {
        cmd_uptime();
    } else if (str_eq(line, "debug")) {
        debug();
    } else if (str_eq(line, "ls")) {
        cmd_ls();
    } else if (line[0] == 'w' && line[1] == 'r' && line[2] == 'i' && line[3] == 't' &&
               line[4] == 'e' && (line[5] == ' ' || line[5] == '\0')) {
        cmd_write(line + 5);
    } else if (line[0] == 'c' && line[1] == 'a' && line[2] == 't' &&
               (line[3] == ' ' || line[3] == '\0')) {
        cmd_cat(line + 3); 
    } else if (line[0] == 'd' && line[1] == 'e' && line[2] == 'l' && line[3] == 'e' &&
               line[4] == 't' && line[5] == 'e' && (line[6] == ' ' || line[6] == '\0')) {
        cmd_delete(line + 6);
    } else if (line[0] == 'a' && line[1] == 'p' && line[2] == 'p' && line[3] == 'e' &&
               line[4] == 'n' && line[5] == 'd' && (line[6] == ' ' || line[6] == '\0')) {
        cmd_append(line + 6);  
    } else if (str_eq(line, "top")) {
        cmd_top();
    } else if (line[0] == 's' && line[1] == 't' && line[2] == 'a' && line[3] == 't' &&
               (line[4] == ' ' || line[4] == '\0')) {
        cmd_stat(line + 4);
    } else if (line[0] == 'm' && line[1] == 'v' && (line[2] == ' ' || line[2] == '\0')) {
        cmd_mv(line + 2);
    } else if (line[0] == 'm' && line[1] == 'k' && line[2] == 'd' && line[3] == 'i' &&
               line[4] == 'r' && (line[5] == ' ' || line[5] == '\0')) {
        cmd_mkdir(line + 5);
    } else if (line[0] == 'c' && line[1] == 'd' && (line[2] == ' ' || line[2] == '\0')) {
        cmd_cd(line + 2);
    } else if (str_eq(line, "pwd")) {
        cmd_pwd();
    } else if (str_eq(line, "history")) {
        cmd_history(); 
    } else if (str_eq(line, "set_max_task")) {
        kprint("Error");
        error_command_no_work_C();
        kprint("\n");
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
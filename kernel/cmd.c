#include "types.h"
#include "string.h"
#include "screen.h"
#include "keyb.h"
#include "vfs.h"
#include "pe.h"
#include "process.h"
#include "mem.h"
#include "io.h"
#include "fat.h"
#include "timer.h"
#include "paging.h"
#include "gui.h"

extern int gui_term_win;

#define CMD_LINE_MAX 256
#define CMD_HISTORY 16
#define MAX_ARGS 32

static int pager_line = 0;

static void more_prompt(void)
{
    screen_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
    int x, y;
    screen_get_cursor(&x, &y);
    screen_set_cursor(0, VGA_HEIGHT - 1);
    printf("-- More --");
    keyb_getchar();
    screen_set_cursor(x, y);
    printf("\r          \r");
    pager_line = VGA_HEIGHT - 2;
}

static void pager_putchar(char c)
{
    screen_putchar(c);
    if (c == '\n')
    {
        pager_line++;
        if (pager_line >= VGA_HEIGHT - 2)
            more_prompt();
    }
}

static void pager_write(const char* str)
{
    for (int i = 0; str[i]; i++)
        pager_putchar(str[i]);
}

static void resolve_path(const char* name, char* out);
static int run_external(const char* name, char* args);

static char current_dir[256] = "\\";
static char cmd_history[CMD_HISTORY][CMD_LINE_MAX];
static int history_count = 0;
static int history_pos = -1;

static int redirect_fd = -1;
static char redirect_file[256];

static void redirect_write_char(char c);

static void redirect_start(const char* path)
{
    if (redirect_fd >= 0) return;
    redirect_fd = vfs_open(path, O_WRITE | O_CREAT);
    if (redirect_fd < 0)
    {
        redirect_fd = -1;
        printf("Cannot redirect output to '%s'.\n", path);
        return;
    }
    screen_set_redirect(redirect_write_char);
}

static void redirect_end(void)
{
    if (redirect_fd >= 0)
    {
        screen_set_redirect(NULL);
        vfs_close(redirect_fd);
        redirect_fd = -1;
    }
    redirect_file[0] = 0;
}

static int parse_redirect(char* line)
{
    redirect_file[0] = 0;
    redirect_fd = -1;

    char* gt = strchr(line, '>');
    if (!gt) return 0;

    *gt = 0;
    char* fname = gt + 1;
    while (*fname == ' ') fname++;
    // trim trailing spaces
    int len = strlen(fname);
    while (len > 0 && fname[len - 1] == ' ') fname[--len] = 0;

    if (*fname)
    {
        strncpy(redirect_file, fname, 255);
        return 1;
    }
    return 0;
}

#define MAX_ENV_VARS 32
#define MAX_ENV_VAR_LEN 256
static char env_vars[MAX_ENV_VARS][MAX_ENV_VAR_LEN];
static int env_var_count = 0;

static const char* env_get(const char* name)
{
    int nlen = strlen(name);
    for (int i = 0; i < env_var_count; i++)
    {
        if (strncmp(env_vars[i], name, nlen) == 0 && env_vars[i][nlen] == '=')
            return env_vars[i] + nlen + 1;
    }
    return NULL;
}

static void env_set(const char* name, const char* value)
{
    int nlen = strlen(name);
    for (int i = 0; i < env_var_count; i++)
    {
        if (strncmp(env_vars[i], name, nlen) == 0 && env_vars[i][nlen] == '=')
        {
            if (value)
            {
                strcpy(env_vars[i], name);
                strcat(env_vars[i], "=");
                strcat(env_vars[i], value);
            }
            else
            {
                for (int j = i; j < env_var_count - 1; j++)
                    strcpy(env_vars[j], env_vars[j + 1]);
                env_var_count--;
            }
            return;
        }
    }
    if (value && env_var_count < MAX_ENV_VARS)
    {
        strcpy(env_vars[env_var_count], name);
        strcat(env_vars[env_var_count], "=");
        strcat(env_vars[env_var_count], value);
        env_var_count++;
    }
}

static void env_list(void)
{
    for (int i = 0; i < env_var_count; i++)
    {
        char* eq = strchr(env_vars[i], '=');
        if (eq)
        {
            printf("%s=", env_vars[i]);
            printf("%s\n", eq + 1);
        }
    }
}

static int expand_env_vars(char* out, const char* in, int max_out)
{
    int oi = 0;
    for (int i = 0; in[i] && oi < max_out - 1; i++)
    {
        if (in[i] == '%')
        {
            i++;
            char varname[128];
            int vi = 0;
            while (in[i] && in[i] != '%' && vi < 127)
                varname[vi++] = in[i++];
            varname[vi] = 0;
            if (in[i] == '%')
            {
                const char* val = env_get(varname);
                if (val)
                {
                    int vlen = strlen(val);
                    int space = max_out - oi - 1;
                    if (vlen > space) vlen = space;
                    memcpy(out + oi, val, vlen);
                    oi += vlen;
                }
            }
            else
            {
                out[oi++] = '%';
                i -= vi + 1;
            }
        }
        else
        {
            out[oi++] = in[i];
        }
    }
    out[oi] = 0;
    return oi;
}

static void env_init(void)
{
    env_set("PATH", "\\SYSTEM");
}

static void build_prompt(char* buf, int max_len)
{
    buf[0] = 'C';
    buf[1] = ':';
    strncpy(buf + 2, current_dir, max_len - 3);
    buf[max_len - 2] = 0;
    int len = strlen(buf);
    if (len < max_len - 1)
    {
        buf[len] = '>';
        buf[len + 1] = 0;
    }
}

static void cmd_exit(void)
{
    printf("\n");
    process_exit(0);
}

static void redirect_write_char(char c)
{
    if (redirect_fd >= 0)
        vfs_write(redirect_fd, &c, 1);
}

static void cmd_echo(char* args)
{
    if (!args || !*args)
    {
        printf("\n");
        return;
    }
    printf("%s\n", args);
}

static void cmd_cd(char* args)
{
    if (!args || !*args)
    {
        printf("C:%s\n", current_dir);
        return;
    }

    if (strcmp(args, ".") == 0)
    {
        printf("C:%s\n", current_dir);
        return;
    }

    if (strcmp(args, "..") == 0)
    {
        int len = strlen(current_dir);
        if (len <= 1) return;
        int i;
        for (i = len - 1; i >= 0; i--)
        {
            if (current_dir[i] == '\\')
                break;
        }
        if (i == 0)
        {
            current_dir[1] = 0;
        }
        else
        {
            current_dir[i] = 0;
        }
        return;
    }

    char new_dir[256];
    if (args[0] == '\\' || args[0] == '/')
    {
        strncpy(new_dir, args, 255);
    }
    else
    {
        strncpy(new_dir, current_dir, 255);
        int len = strlen(new_dir);
        if (new_dir[len - 1] != '\\')
            strcat(new_dir, "\\");
        strcat(new_dir, args);
    }

    int len = strlen(new_dir);
    if (len > 1 && new_dir[len - 1] == '\\')
        new_dir[len - 1] = 0;

    if (!vfs_exists(new_dir))
    {
        printf("The system cannot find the path specified.\n");
        return;
    }

    strncpy(current_dir, new_dir, 255);
}

static void cmd_dir(char* args)
{
    UNUSED(args);

    char full_path[256];
    strcpy(full_path, current_dir);
    if (full_path[0] == 0) strcpy(full_path, "\\");

    printf(" Volume in drive C is %s\n", fat_get_volume_label());
    printf(" Directory of C:%s\n\n", full_path);

    char entries[1024];
    memset(entries, 0, 1024);

    int count = vfs_readdir(full_path, entries, 1024);
    if (count < 0)
    {
        printf(" File Not Found\n");
        return;
    }

    int total_files = 0;
    int total_dirs = 0;
    char* p = entries;
    while (*p)
    {
        char type = p[0];
        char* name = p + 2;
        int nlen = strlen(name);
        char* size_str = name + nlen + 1;

        if (type == 'D')
        {
            printf("    <DIR>   ");
            total_dirs++;
        }
        else
        {
            printf("            ");
            total_files++;
        }

        screen_write(name);

        if (type != 'D' && *size_str)
        {
            int sw = 0;
            while (sw < 12 - (int)strlen(size_str)) { printf(" "); sw++; }
            printf(" %s", size_str);
        }

        printf("\n");
        p = size_str + strlen(size_str) + 1;
    }

    printf("        %d File(s)\n", total_files);
}

static void cmd_type(char* args)
{
    if (!args || !*args)
    {
        printf("The syntax of the command is incorrect.\n");
        return;
    }

    char full_path[256];
    resolve_path(args, full_path);

    int fd = vfs_open(full_path, 0);
    if (fd < 0)
    {
        printf("The system cannot find the file specified.\n");
        return;
    }

    char buf[512];
    pager_line = 0;
    for (;;)
    {
        int bytes = vfs_read(fd, buf, 512);
        if (bytes <= 0) break;
        for (int i = 0; i < bytes; i++)
        {
            if (buf[i] == 0) goto done;
            pager_putchar(buf[i]);
        }
    }
done:
    vfs_close(fd);
    if (pager_line > 0 && pager_line < VGA_HEIGHT - 2)
        printf("\n");
}

static void cmd_mkdir(char* args)
{
    if (!args || !*args)
    {
        printf("The syntax of the command is incorrect.\n");
        return;
    }

    char full_path[256];
    if (args[0] == '\\' || args[0] == '/')
    {
        strncpy(full_path, args, 255);
    }
    else
    {
        strncpy(full_path, current_dir, 255);
        int len = strlen(full_path);
        if (full_path[len - 1] != '\\')
            strcat(full_path, "\\");
        strcat(full_path, args);
    }

    if (vfs_mkdir(full_path) < 0)
    {
        printf("A subdirectory or file already exists.\n");
    }
}

static void cmd_rmdir(char* args)
{
    if (!args || !*args)
    {
        printf("The syntax of the command is incorrect.\n");
        return;
    }

    char full_path[256];
    if (args[0] == '\\' || args[0] == '/')
    {
        strncpy(full_path, args, 255);
    }
    else
    {
        strncpy(full_path, current_dir, 255);
        int len = strlen(full_path);
        if (full_path[len - 1] != '\\')
            strcat(full_path, "\\");
        strcat(full_path, args);
    }

    if (vfs_unlink(full_path) < 0)
    {
        printf("The system cannot find the file specified.\n");
    }
}

static void cmd_del(char* args)
{
    if (!args || !*args)
    {
        printf("The syntax of the command is incorrect.\n");
        return;
    }

    char full_path[256];
    if (args[0] == '\\' || args[0] == '/')
    {
        strncpy(full_path, args, 255);
    }
    else
    {
        strncpy(full_path, current_dir, 255);
        int len = strlen(full_path);
        if (full_path[len - 1] != '\\')
            strcat(full_path, "\\");
        strcat(full_path, args);
    }

    if (vfs_unlink(full_path) < 0)
    {
        printf("The system cannot find the file specified.\n");
    }
}

static uint8_t cmos_read(uint8_t reg)
{
    outb(0x70, (reg & 0x7F) | 0x80);
    uint8_t val = inb(0x71);
    outb(0x70, 0x00);
    return val;
}

static uint8_t bcd_to_bin(uint8_t bcd)
{
    return (bcd & 0x0F) + ((bcd >> 4) * 10);
}

static void cmd_copy(char* args)
{
    if (!args || !*args)
    {
        printf("The syntax of the command is incorrect.\n");
        return;
    }

    char src[256], dst[256];
    int si = 0;
    while (*args == ' ') args++;
    while (*args && *args != ' ' && si < 255) src[si++] = *args++;
    src[si] = 0;
    while (*args == ' ') args++;
    si = 0;
    while (*args && *args != ' ' && si < 255) dst[si++] = *args++;
    dst[si] = 0;

    if (!src[0] || !dst[0])
    {
        printf("The syntax of the command is incorrect.\n");
        return;
    }

    char src_path[256], dst_path[256];
    if (src[0] == '\\' || src[0] == '/')
        strncpy(src_path, src, 255);
    else
    {
        strncpy(src_path, current_dir, 255);
        if (src_path[strlen(src_path) - 1] != '\\') strcat(src_path, "\\");
        strcat(src_path, src);
    }
    if (dst[0] == '\\' || dst[0] == '/')
        strncpy(dst_path, dst, 255);
    else
    {
        strncpy(dst_path, current_dir, 255);
        if (dst_path[strlen(dst_path) - 1] != '\\') strcat(dst_path, "\\");
        strcat(dst_path, dst);
    }

    int fd = vfs_open(src_path, 0);
    if (fd < 0)
    {
        printf("The system cannot find the file specified.\n");
        return;
    }

    char* buf = malloc(65536);
    if (!buf) { vfs_close(fd); return; }
    int bytes = vfs_read(fd, buf, 65536);
    vfs_close(fd);

    if (bytes <= 0) { free(buf); printf("Cannot read file.\n"); return; }

    int wfd = vfs_open(dst_path, O_CREAT);
    if (wfd < 0 || vfs_write(wfd, buf, bytes) < 0)
    {
        free(buf);
        printf("Cannot write to destination.\n");
        return;
    }
    vfs_close(wfd);
    free(buf);
    printf("        1 file(s) copied.\n");
}

static void resolve_path(const char* name, char* out)
{
    if (name[0] == '\\' || name[0] == '/')
        strncpy(out, name, 255);
    else
    {
        strncpy(out, current_dir, 255);
        int len = strlen(out);
        if (out[len - 1] != '\\') strcat(out, "\\");
        strcat(out, name);
    }
}

static void cmd_ren(char* args)
{
    if (!args || !*args)
    {
        printf("The syntax of the command is incorrect.\n");
        return;
    }

    char old_name[256], new_name[256];
    int si = 0;
    while (*args == ' ') args++;
    while (*args && *args != ' ' && si < 255) old_name[si++] = *args++;
    old_name[si] = 0;
    while (*args == ' ') args++;
    si = 0;
    while (*args && *args != ' ' && si < 255) new_name[si++] = *args++;
    new_name[si] = 0;

    if (!old_name[0] || !new_name[0])
    {
        printf("The syntax of the command is incorrect.\n");
        return;
    }

    char old_path[256], new_path[256];
    resolve_path(old_name, old_path);
    resolve_path(new_name, new_path);

    if (vfs_rename(old_path, new_path) < 0)
        printf("The system cannot find the file specified.\n");
}

static void cmd_move(char* args)
{
    if (!args || !*args)
    {
        printf("The syntax of the command is incorrect.\n");
        return;
    }

    char src[256], dst[256];
    int si = 0;
    while (*args == ' ') args++;
    while (*args && *args != ' ' && si < 255) src[si++] = *args++;
    src[si] = 0;
    while (*args == ' ') args++;
    si = 0;
    while (*args && *args != ' ' && si < 255) dst[si++] = *args++;
    dst[si] = 0;

    if (!src[0] || !dst[0])
    {
        printf("The syntax of the command is incorrect.\n");
        return;
    }

    char src_path[256], dst_path[256];
    resolve_path(src, src_path);
    resolve_path(dst, dst_path);

    if (vfs_rename(src_path, dst_path) == 0)
        return;

    char* buf = malloc(65536);
    if (!buf) return;
    int fd = vfs_open(src_path, 0);
    if (fd < 0) { free(buf); printf("The system cannot find the file specified.\n"); return; }
    int bytes = vfs_read(fd, buf, 65536);
    vfs_close(fd);
    if (bytes <= 0) { free(buf); return; }

    int wfd = vfs_open(dst_path, O_CREAT);
    if (wfd >= 0) { vfs_write(wfd, buf, bytes); vfs_close(wfd); }
    free(buf);
    vfs_unlink(src_path);
}

static void cmd_vol(void)
{
    printf(" Volume in drive C is %s\n", fat_get_volume_label());
}

static void cmd_date(void)
{
    uint8_t day = bcd_to_bin(cmos_read(0x07));
    uint8_t month = bcd_to_bin(cmos_read(0x08));
    uint8_t year = bcd_to_bin(cmos_read(0x09));
    uint8_t century = bcd_to_bin(cmos_read(0x32));
    printf("The current date is: %02d/%02d/%02d%02d\n", month, day, century, year);
}

static void cmd_time(void)
{
    uint8_t hours = bcd_to_bin(cmos_read(0x04));
    uint8_t minutes = bcd_to_bin(cmos_read(0x02));
    uint8_t seconds = bcd_to_bin(cmos_read(0x00));
    printf("The current time is: %02d:%02d:%02d\n", hours, minutes, seconds);
}

static void cmd_pause(void)
{
    printf("Press any key to continue . . . ");
    keyb_getchar();
    printf("\n");
}

static void cmd_color(char* args)
{
    if (!args || !*args || !args[1])
    {
        screen_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
        return;
    }

    uint8_t fg = 7, bg = 0;
    char c = args[0];
    if (c >= '0' && c <= '9') fg = c - '0';
    else if (c >= 'A' && c <= 'F') fg = c - 'A' + 10;
    else if (c >= 'a' && c <= 'f') fg = c - 'a' + 10;

    c = args[1];
    if (c >= '0' && c <= '9') bg = c - '0';
    else if (c >= 'A' && c <= 'F') bg = c - 'A' + 10;
    else if (c >= 'a' && c <= 'f') bg = c - 'a' + 10;

    screen_set_color(fg, bg);
}

static void cmd_mem(void)
{
    printf("\n");
    printf("  Total physical memory: %d MB\n", paging_get_total_frames() * 4 / 1024);
    printf("  Used physical frames:  %d KB\n", paging_get_used_frames() * 4);
    printf("  Free physical frames:  %d KB\n", (paging_get_total_frames() - paging_get_used_frames()) * 4);
    printf("  Heap used:             %d bytes\n", mem_get_used());
    printf("  Heap free:             %d bytes\n", mem_get_free());
    printf("  Timer ticks:           %d\n", (uint32_t)timer_get_ticks());
    printf("\n");
}

static void cmd_more(char* args)
{
    if (!args || !*args)
    {
        printf("The syntax of the command is incorrect.\n");
        return;
    }
    cmd_type(args);
}

static void cmd_sort(char* args)
{
    if (!args || !*args)
    {
        printf("The syntax of the command is incorrect.\n");
        return;
    }

    char full_path[256];
    resolve_path(args, full_path);

    int fd = vfs_open(full_path, 0);
    if (fd < 0)
    {
        printf("The system cannot find the file specified.\n");
        return;
    }

    char* file_buf = malloc(32768);
    if (!file_buf) { vfs_close(fd); return; }
    int bytes = vfs_read(fd, file_buf, 32768);
    vfs_close(fd);
    if (bytes <= 0) { free(file_buf); return; }
    file_buf[bytes] = 0;

    char* lines[1024];
    int line_count = 0;
    lines[line_count++] = file_buf;

    for (int i = 0; i < bytes && line_count < 1024; i++)
    {
        if (file_buf[i] == '\n')
        {
            file_buf[i] = 0;
            if (i + 1 < bytes)
                lines[line_count++] = &file_buf[i + 1];
        }
    }

    for (int i = 0; i < line_count - 1; i++)
    {
        for (int j = 0; j < line_count - i - 1; j++)
        {
            if (strcmp(lines[j], lines[j + 1]) > 0)
            {
                char* tmp = lines[j];
                lines[j] = lines[j + 1];
                lines[j + 1] = tmp;
            }
        }
    }

    pager_line = 0;
    for (int i = 0; i < line_count; i++)
    {
        pager_write(lines[i]);
        pager_putchar('\n');
    }
    free(file_buf);
}

static void cmd_find(char* args)
{
    if (!args || !*args)
    {
        printf("The syntax of the command is incorrect.\n");
        return;
    }

    while (*args == ' ') args++;
    if (*args != '"')
    {
        printf("FIND: Invalid syntax. Use FIND \"text\" filename\n");
        return;
    }
    args++;
    char search[256];
    int si = 0;
    while (*args && *args != '"' && si < 255) search[si++] = *args++;
    search[si] = 0;
    if (*args == '"') args++;
    while (*args == ' ') args++;

    if (!*args)
    {
        printf("FIND: Invalid syntax. Use FIND \"text\" filename\n");
        return;
    }

    char full_path[256];
    resolve_path(args, full_path);

    int fd = vfs_open(full_path, 0);
    if (fd < 0)
    {
        printf("The system cannot find the file specified.\n");
        return;
    }

    char* file_buf = malloc(32768);
    if (!file_buf) { vfs_close(fd); return; }
    int bytes = vfs_read(fd, file_buf, 32768);
    vfs_close(fd);
    if (bytes <= 0) { free(file_buf); return; }
    file_buf[bytes] = 0;

    int found = 0;
    int line_num = 1;
    pager_line = 0;
    char* p = file_buf;
    while (*p)
    {
        char* next = p;
        while (*next && *next != '\n') next++;
        char saved = *next;
        *next = 0;

        if (strstr(p, search))
        {
            char line_buf[96];
            int li = 0;
            for (int i = 0; p[i] && p[i] != '\r' && li < 94; i++)
                line_buf[li++] = p[i];
            line_buf[li] = 0;

            char num[16];
            int ni = 0;
            int t = line_num;
            if (t == 0) num[ni++] = '0';
            while (t > 0) { num[ni++] = '0' + (t % 10); t /= 10; }
            num[ni] = 0;
            for (int i = 0; i < ni / 2; i++) { char c = num[i]; num[i] = num[ni - i - 1]; num[ni - i - 1] = c; }

            char out[128];
            int oi = 0;
            for (int i = 0; num[i]; i++) out[oi++] = num[i];
            out[oi++] = ':'; out[oi++] = ' ';
            for (int i = 0; line_buf[i]; i++) out[oi++] = line_buf[i];
            out[oi++] = '\n'; out[oi] = 0;

            pager_write(out);
            found++;
        }

        *next = saved;
        if (saved == '\n') p = next + 1;
        else break;
        line_num++;
    }

    free(file_buf);
    if (!found)
        pager_write("FIND: No match found.\n");
}

static void cmd_set(char* args)
{
    if (!args || !*args)
    {
        env_list();
        return;
    }

    char* eq = strchr(args, '=');
    if (eq)
    {
        *eq = 0;
        char* name = args;
        char* value = eq + 1;
        if (*value)
            env_set(name, value);
        else
            env_set(name, NULL);
    }
    else
    {
        printf("Usage: SET variable=value\n");
    }
}

static void cmd_path(char* args)
{
    if (!args || !*args)
    {
        const char* p = env_get("PATH");
        if (p)
            printf("PATH=%s\n", p);
        else
            printf("PATH not set\n");
    }
    else
    {
        env_set("PATH", args);
    }
}

static void cmd_cls(void)
{
    if (gui_term_win >= 0)
        gui_clear(gui_term_win);
    else
        screen_clear();
}

static void cmd_ver(void)
{
    printf("\n");  
    printf("BlueOS x86_64 version 1.0\n");
    printf("Copyright 2026 Blue OS Corporation.\n");
    printf("Compatibility: Windows PE32+ / FAT32\n");
    printf("\n");
}

static void cmd_help(void)
{
    pager_line = 0;
    pager_write("\n");
    pager_write("Blue OS Command Interpreter - Help\n");
    pager_write("====================================\n\n");
    pager_write("  CD      Change current directory\n");
    pager_write("  CLS     Clear the screen\n");
    pager_write("  COLOR   Set console colors (COLOR FG BG)\n");
    pager_write("  COPY    Copy file(s) to another location\n");
    pager_write("  DATE    Display the system date\n");
    pager_write("  DEL     Delete one or more files\n");
    pager_write("  DIR     List directory contents\n");
    pager_write("  ECHO    Display messages\n");
    pager_write("  EXIT    Quit the command interpreter\n");
    pager_write("  FIND    Search for text in a file\n");
    pager_write("  HELP    Show this help\n");
    pager_write("  MD      Create a directory\n");
    pager_write("  MEM     Display memory usage\n");
    pager_write("  MKDIR   Create a directory\n");
    pager_write("  MORE    Display file contents page by page\n");
    pager_write("  MOVE    Move/rename a file\n");
    pager_write("  PATH    Display or set the search path\n");
    pager_write("  PAUSE   Wait for a key press\n");
    pager_write("  RD      Remove a directory\n");
    pager_write("  REN     Rename a file\n");
    pager_write("  ERASE   Delete one or more files\n");
    pager_write("  RMDIR   Remove a directory\n");
    pager_write("  SET     Display/set environment variables\n");
    pager_write("  SORT    Sort lines in a text file\n");
    pager_write("  TIME    Display the system time\n");
    pager_write("  TYPE    Display file contents\n");
    pager_write("  VER     Display version information\n");
    pager_write("  VOL     Display volume label\n");
    pager_write("  <prog>  Run an executable (.exe)\n");
    pager_write("\n");
}

static int search_path(const char* name, char* out_path, int has_ext)
{
    const char* path_str = env_get("PATH");
    if (!path_str) return -1;

    char path_copy[256];
    strncpy(path_copy, path_str, 255);
    path_copy[255] = 0;

    char* dir = path_copy;
    while (*dir)
    {
        char* next = strchr(dir, ';');
        if (next) *next = 0;

        strcpy(out_path, dir);
        int len = strlen(out_path);
        if (len > 0 && out_path[len - 1] != '\\')
            strcat(out_path, "\\");
        strcat(out_path, name);
        if (!has_ext) strcat(out_path, ".exe");

        if (pe_check_format(out_path))
            return 0;

        if (next)
            dir = next + 1;
        else
            break;
    }
    return -1;
}

static void execute_dispatch(const char* cmd, char* args);

static void execute_line(char* line)
{
    char cmd[64];
    char* args = NULL;
    int i = 0;

    while (line[i] && line[i] != ' ' && i < 63)
    {
        cmd[i] = line[i] >= 'a' && line[i] <= 'z' ? line[i] - 32 : line[i];
        i++;
    }
    cmd[i] = 0;

    if (line[i] == ' ')
    {
        i++;
        while (line[i] == ' ') i++;
        args = &line[i];
        while (*args == ' ') args++;
        if (*args == 0) args = NULL;
    }

    execute_dispatch(cmd, args);
}

static void execute_dispatch(const char* cmd, char* args)
{
    if (strcmp(cmd, "EXIT") == 0)
    {
        redirect_end();
        cmd_exit();
        return;
    }
    else if (strcmp(cmd, "ECHO") == 0)
        cmd_echo(args);
    else if (strcmp(cmd, "CD") == 0)
        cmd_cd(args);
    else if (strcmp(cmd, "DIR") == 0)
        cmd_dir(args);
    else if (strcmp(cmd, "TYPE") == 0)
        cmd_type(args);
    else if (strcmp(cmd, "CLS") == 0)
        cmd_cls();
    else if (strcmp(cmd, "VER") == 0)
        cmd_ver();
    else if (strcmp(cmd, "HELP") == 0)
        cmd_help();
    else if (strcmp(cmd, "MKDIR") == 0 || strcmp(cmd, "MD") == 0)
        cmd_mkdir(args);
    else if (strcmp(cmd, "RMDIR") == 0 || strcmp(cmd, "RD") == 0)
        cmd_rmdir(args);
    else if (strcmp(cmd, "DEL") == 0 || strcmp(cmd, "ERASE") == 0)
        cmd_del(args);
    else if (strcmp(cmd, "COPY") == 0)
        cmd_copy(args);
    else if (strcmp(cmd, "REN") == 0 || strcmp(cmd, "RENAME") == 0)
        cmd_ren(args);
    else if (strcmp(cmd, "VOL") == 0)
        cmd_vol();
    else if (strcmp(cmd, "DATE") == 0)
        cmd_date();
    else if (strcmp(cmd, "TIME") == 0)
        cmd_time();
    else if (strcmp(cmd, "PAUSE") == 0)
        cmd_pause();
    else if (strcmp(cmd, "COLOR") == 0)
        cmd_color(args);
    else if (strcmp(cmd, "MEM") == 0)
        cmd_mem();
    else if (strcmp(cmd, "MOVE") == 0)
        cmd_move(args);
    else if (strcmp(cmd, "MORE") == 0)
        cmd_more(args);
    else if (strcmp(cmd, "FIND") == 0)
        cmd_find(args);
    else if (strcmp(cmd, "SORT") == 0)
        cmd_sort(args);
    else if (strcmp(cmd, "SET") == 0)
        cmd_set(args);
    else if (strcmp(cmd, "PATH") == 0)
        cmd_path(args);
    else if (cmd[0] != 0)
    {
        if (run_external(cmd, args) != 0)
            printf("'%s' is not recognized as an internal or external command,\noperable program or batch file.\n", cmd);
    }
}

static int run_batch(const char* path)
{
    int fd = vfs_open(path, 0);
    if (fd < 0) return -1;

    char* buf = malloc(32768);
    if (!buf) { vfs_close(fd); return -1; }

    int bytes = vfs_read(fd, buf, 32767);
    vfs_close(fd);
    if (bytes <= 0) { free(buf); return -1; }
    buf[bytes] = 0;

    printf("[BAT] Executing '%s'...\n", path);

    char* p = buf;
    int line_num = 0;
    while (*p)
    {
        while (*p == ' ' || *p == 9) p++;
        if (*p == 0) break;

        char* nl = p;
        while (*nl && *nl != '\n' && *nl != '\r') nl++;
        char saved = *nl;
        *nl = 0;

        if (*p && *p != ';' && *p != ':')
        {
            line_num++;
            printf("C:%s>%s\n", current_dir, p);
            execute_line(p);
        }

        if (saved == 0) break;
        *nl = saved;
        p = nl + 1;
        while (*p == '\r' || *p == '\n') p++;
    }

    free(buf);
    printf("[BAT] Done (%d lines)\n", line_num);
    return 0;
}

static int run_external(const char* name, char* args)
{
    UNUSED(args);

    char path[256];
    int found = 0;
    int is_bat = 0;

    int has_ext = 0;
    int name_len = strlen(name);
    for (int i = 0; i < name_len; i++)
        if (name[i] == '.') has_ext = 1;

    if (has_ext && (strcmp(name + name_len - 4, ".bat") == 0 || strcmp(name + name_len - 4, ".BAT") == 0))
        is_bat = 1;

    // Try current directory
    strcpy(path, current_dir);
    strcat(path, "\\");
    strcat(path, name);
    if (!has_ext) strcat(path, is_bat ? ".BAT" : ".EXE");

    if (is_bat)
    {
        if (vfs_exists(path))
            return run_batch(path);
    }
    else if (pe_check_format(path))
    {
        found = 1;
    }

    // Try \SYSTEM\
    if (!found && !is_bat)
    {
        char prog_path[256] = "\\SYSTEM\\";
        strcat(prog_path, name);
        if (!has_ext) strcat(prog_path, ".EXE");

        if (pe_check_format(prog_path))
        {
            strcpy(path, prog_path);
            found = 1;
        }
    }

    // Try PATH directories
    if (!found && !is_bat)
    {
        if (search_path(name, path, has_ext) == 0)
            found = 1;
    }

    if (found)
    {
        printf("[CMD] Starting '%s'...\n", path);
        int pid = pe_spawn(path);
        if (pid > 0)
        {
            printf("[CMD] PID %d spawned, waiting...\n", pid);
            process_wait((uint32_t)pid);
            printf("[CMD] Program exited\n");
        }
        else
            printf("Failed to start program.\n");
        return 0;
    }

    return -1;
}

void cmd_run(void)
{
    char line[CMD_LINE_MAX];
    int pos = 0;
    char prompt[64];

    env_init();


    run_batch("\\SYSTEM\\AUTOEXEC.BAT");

    build_prompt(prompt, 64);
    screen_set_color(COLOR_LIGHT_GREEN, COLOR_BLACK);
    printf("%s", prompt);
    screen_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);

    while (1)
    {
        if (cmd_should_exit) { printf("\n"); process_exit(0); }
        char c = keyb_getchar();

        if (c == '\n' || c == '\r')
        {
            printf("\n");
            line[pos] = 0;

            if (pos > 0)
            {
                strncpy(cmd_history[history_count % CMD_HISTORY], line, CMD_LINE_MAX - 1);
                history_count++;
            }
            history_pos = -1;

            if (line[0] != 0)
            {
                char expanded[CMD_LINE_MAX];
                expand_env_vars(expanded, line, CMD_LINE_MAX);
                strcpy(line, expanded);

                parse_redirect(line);

                char cmd[64];
                char* args = NULL;
                int i = 0;

                while (line[i] && line[i] != ' ' && i < 63)
                {
                    cmd[i] = line[i] >= 'a' && line[i] <= 'z' ? line[i] - 32 : line[i];
                    i++;
                }
                cmd[i] = 0;

                if (line[i] == ' ')
                {
                    i++;
                    while (line[i] == ' ') i++;
                    args = &line[i];
                    while (*args == ' ') args++;
                    if (*args == 0) args = NULL;
                }

                if (redirect_file[0])
                    redirect_start(redirect_file);

                execute_dispatch(cmd, args);
                redirect_end();
            }

            pos = 0;
            build_prompt(prompt, 64);
            screen_set_color(COLOR_LIGHT_GREEN, COLOR_BLACK);
            printf("%s", prompt);
            screen_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
        }
        else if (c == '\b')
        {
            if (pos > 0)
            {
                pos--;
                screen_putchar('\b');
            }
        }
        else if (c >= ' ' && pos < CMD_LINE_MAX - 1)
        {
            line[pos++] = c;
            screen_putchar(c);
        }
    }
}

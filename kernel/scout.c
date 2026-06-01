#include "types.h"
#include "string.h"
#include "mem.h"
#include "screen.h"
#include "fb.h"
#include "gui.h"
#include "vfs.h"
#include "process.h"
#include "pe.h"
#include "font.h"

#define SCOUT_MAX_ENTRIES 256
#define SCOUT_PATH_MAX 256

typedef struct {
    char type;
    char name[64];
    char size[16];
} scout_entry_t;

static scout_entry_t scout_entries[SCOUT_MAX_ENTRIES];
static int scout_num_entries = 0;
static char scout_current_dir[SCOUT_PATH_MAX] = "\\SYSTEM\\PROGRAMS";
static int scout_win = -1;

static void scout_load_dir(const char* path)
{
    scout_num_entries = 0;
    char entries[4096];
    memset(entries, 0, sizeof(entries));

    if (vfs_readdir(path, entries, sizeof(entries)) <= 0)
        return;

    char* p = entries;
    while (*p && scout_num_entries < SCOUT_MAX_ENTRIES)
    {
        scout_entry_t* e = &scout_entries[scout_num_entries];
        e->type = p[0];
        char* name = p + 2;
        int nlen = strlen(name);
        if (nlen >= (int)sizeof(e->name)) nlen = sizeof(e->name) - 1;
        memcpy(e->name, name, nlen);
        e->name[nlen] = 0;
        char* size_str = name + nlen + 1;
        int slen = strlen(size_str);
        if (slen >= (int)sizeof(e->size)) slen = sizeof(e->size) - 1;
        memcpy(e->size, size_str, slen);
        e->size[slen] = 0;
        scout_num_entries++;
        p = size_str + slen + 1;
    }
}

static void scout_refresh_content(void)
{
    if (scout_win < 0) return;

    scout_load_dir(scout_current_dir);
    gui_clear(scout_win);

    gui_puts(scout_win, " ");

    char path_display[SCOUT_PATH_MAX + 10];
    int pi = 0;
    path_display[pi++] = '\\';
    for (int i = 0; scout_current_dir[i] && pi < SCOUT_PATH_MAX + 8; i++)
        path_display[pi++] = scout_current_dir[i];
    path_display[pi] = 0;
    gui_puts(scout_win, path_display);
    gui_putchar(scout_win, '\n');
    gui_putchar(scout_win, '\n');

    char line[128];
    for (int i = 0; i < scout_num_entries; i++)
    {
        scout_entry_t* e = &scout_entries[i];
        int li = 0;
        if (e->type == 'D')
        {
            line[li++] = '[';
            int ni;
            for (ni = 0; e->name[ni] && ni < 60; ni++)
                line[li++] = e->name[ni];
            line[li++] = ']';
        }
        else
        {
            for (int ni = 0; e->name[ni] && ni < 62; ni++)
                line[li++] = e->name[ni];
            while (li < 50) line[li++] = ' ';
            for (int si = 0; e->size[si] && si < 14 && li < 126; si++)
                line[li++] = e->size[si];
        }
        line[li] = 0;
        gui_puts(scout_win, line);
        gui_putchar(scout_win, '\n');
    }
}

static void scout_navigate(const char* path)
{
    if (!vfs_exists(path)) return;

    strncpy(scout_current_dir, path, SCOUT_PATH_MAX - 1);

    char title[32];
    int ti = 0;
    const char* t = "Scout - ";
    for (int i = 0; t[i]; i++) title[ti++] = t[i];
    for (int i = 1; scout_current_dir[i] && ti < 30; i++) title[ti++] = scout_current_dir[i];
    title[ti] = 0;
    gui_set_title(scout_win, title);

    scout_refresh_content();
}

static int has_exe_ext(const char* name)
{
    int len = strlen(name);
    if (len < 5) return 0;
    return (name[len - 4] == '.' &&
            (name[len - 3] == 'E' || name[len - 3] == 'e') &&
            (name[len - 2] == 'X' || name[len - 2] == 'x') &&
            (name[len - 1] == 'E' || name[len - 1] == 'e'));
}

static void scout_on_click(int win_id, int mx, int my)
{
    (void)win_id;
    if (scout_win < 0) return;
    if (mx < 0 || (uint32_t)mx >= fb_info.width || my < 0 || (uint32_t)my >= fb_info.height)
        return;

    int wx = 0, wy = 0;
    gui_get_window_rect(scout_win, &wx, &wy, NULL, NULL);

    int rel_y = my - wy - GUI_TITLE_HEIGHT - 1;
    if (rel_y < 0) return;
    int row = rel_y / FONT_HEIGHT;
    if (row < 2) return;

    int idx = row - 2;
    if (idx < 0 || idx >= scout_num_entries) return;

    scout_entry_t* e = &scout_entries[idx];

    char new_path[SCOUT_PATH_MAX];
    strcpy(new_path, scout_current_dir);
    int len = strlen(new_path);
    if (len > 0 && new_path[len - 1] != '\\')
        strcat(new_path, "\\");
    strcat(new_path, e->name);
    int nl = strlen(new_path);
    if (nl > 1 && new_path[nl - 1] == '\\')
        new_path[nl - 1] = 0;

    if (e->type == 'D')
    {
        if (strcmp(e->name, ".") == 0) return;
        if (strcmp(e->name, "..") == 0)
        {
            int plen = strlen(scout_current_dir);
            if (plen <= 1) return;
            int i;
            for (i = plen - 1; i >= 0; i--)
                if (scout_current_dir[i] == '\\') break;
            if (i <= 0)
                strcpy(new_path, "\\");
            else
            {
                memcpy(new_path, scout_current_dir, i);
                new_path[i] = 0;
            }
        }
        scout_navigate(new_path);
    }
    else if (has_exe_ext(e->name))
    {
        gui_puts(scout_win, "\n Launching ");
        gui_puts(scout_win, e->name);
        gui_puts(scout_win, "...\n");
        int pid = pe_spawn(new_path);
        if (pid > 0)
            process_wait((uint32_t)pid);
        else
            gui_puts(scout_win, " Failed to launch.\n");
        scout_refresh_content();
    }
    (void)scout_num_entries;
}

void scout_run(void)
{
    int win_w = 640;
    int win_h = 420;

    if (fb_info.width > 0 && (int)fb_info.width < win_w + 20)
        win_w = fb_info.width - 20;
    if (fb_info.height > 0 && (int)fb_info.height < win_h + 40)
        win_h = fb_info.height - 40;

    int wx = (fb_info.width - win_w) / 2;
    int wy = (fb_info.height - win_h) / 2;
    if (wx < 10) wx = 10;
    if (wy < 10) wy = 10;

    scout_win = gui_create("Scout - \\", wx, wy, win_w, win_h);
    if (scout_win < 0) return;

    gui_set_content_click_callback(scout_win, scout_on_click);
    scout_refresh_content();

    for (;;)
        yield_to_scheduler();
}

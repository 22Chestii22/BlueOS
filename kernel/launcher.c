#include "types.h"
#include "string.h"
#include "mem.h"
#include "fb.h"
#include "font.h"
#include "module.h"
#include "blu.h"
#include "gui.h"
#include "timer.h"
#include "net.h"

#define LAUNCHER_MAX_RESULTS 6
#define LAUNCHER_SEARCH_LEN 64
#define LAUNCHER_BAR_W 520
#define LAUNCHER_BAR_H 36
#define LAUNCHER_ITEM_H 28
#define LAUNCHER_DESC_H 14
#define LAUNCHER_AI_RESULTS 4

static int launcher_open = 0;
static volatile int launcher_hotkey_flag = 0;
static char search_text[LAUNCHER_SEARCH_LEN];
static int search_pos = 0;
static int selected_result = 0;
static int num_results = 0;
static char ai_generating_msg[128];

static int bar_x, bar_y, bar_w = LAUNCHER_BAR_W, bar_h = LAUNCHER_BAR_H;
static int item_h = LAUNCHER_ITEM_H;

/* Groq API — key set via GROQ_API_KEY env var in relay script, hardcoded key removed */
#define GROQ_API_HOST "api.groq.com"
#define GROQ_RELAY_HOST "10.0.2.2"
#define GROQ_RELAY_PORT 8080

/* ===== Installed programs ===== */
typedef struct {
    const char* name;
    const char* path;
} launcher_app_t;

static launcher_app_t launcher_apps[] = {
    {"CMD", "\\SYSTEM\\PROGRAMS\\CMD.BLU"},
    {"RENDER", "\\SYSTEM\\PROGRAMS\\RENDER.BLU"},
    {"IDLE", "\\SYSTEM\\PROGRAMS\\IDLE.BLU"},
    {"EDIT", "\\SYSTEM\\PROGRAMS\\EDIT.BLU"},
    {"SCOUT", "\\SYSTEM\\PROGRAMS\\SCOUT.BLU"},
    {"TASKMAN", "\\SYSTEM\\PROGRAMS\\TASKMAN.BLU"},
    {NULL, NULL}
};
static int num_launcher_apps = 6;

/* ===== AI Suggestion Engine ===== */
typedef struct {
    const char* keywords;
    const char* app_name;
    const char* desc;
    int icon_r, icon_g, icon_b;
} ai_suggestion_t;

#define AI_DB_SIZE 49
static ai_suggestion_t ai_db[AI_DB_SIZE] = {
    {"calc,math,numbers,count,arithmetic,algebra", "CalcPro", "Scientific calculator with graphing", 0x40, 0x80, 0xC0},
    {"calc,math,numbers,count,arithmetic,algebra", "MathStudio", "Advanced algebra & geometry toolkit", 0x30, 0x90, 0xE0},
    {"calc,math,numbers,count,sum,total", "NumberCruncher", "Spreadsheet & data analysis", 0x20, 0xA0, 0x80},
    {"calc,math,percent,tax,finance", "QuickCalc", "Everyday calculator with history", 0x50, 0x70, 0xD0},

    {"write,text,note,doc,word,type,edit", "NotePad Pro", "Rich text editor with formatting", 0xC0, 0x80, 0x40},
    {"write,text,note,doc,word,type,edit", "TextCraft", "Markdown & code editor", 0x90, 0x60, 0xC0},
    {"write,text,note,doc,journal,dairy", "DocWriter", "Word processor with spellcheck", 0x60, 0x90, 0xC0},
    {"write,text,note,doc,type", "EditMaster", "Advanced text editing suite", 0x80, 0x80, 0xD0},

    {"draw,paint,art,image,sketch,design", "PaintStudio", "Digital painting & illustration", 0xE0, 0x60, 0x60},
    {"draw,paint,art,image,sketch,photo", "ImageEditor", "Photo editing & filters", 0x80, 0xA0, 0x60},
    {"draw,paint,art,sketch,design,create", "ArtCanvas", "Creative drawing & sketching", 0xD0, 0x70, 0xA0},
    {"draw,paint,image,photo,design,graphic", "DesignPro", "Vector graphics & layout", 0x70, 0x90, 0xE0},

    {"game,play,fun,arcade,entertain", "SnakeGame", "Classic snake arcade game", 0x60, 0xC0, 0x60},
    {"game,play,fun,arcade,puzzle,block", "TetrisReborn", "Block-stacking puzzle game", 0x80, 0x60, 0xE0},
    {"game,play,fun,entertain,board,strategy", "ChessMaster", "Chess with AI opponent", 0xC0, 0xA0, 0x60},
    {"game,play,fun,arcade,action,jump", "PixelAdventure", "Side-scrolling platformer", 0x60, 0xC0, 0xC0},

    {"music,sound,audio,listen,song,play", "MusicPlayer", "Audio player with playlist support", 0x80, 0xC0, 0x60},
    {"music,sound,audio,record,song,mix", "SoundStudio", "Audio recording & mixing", 0xC0, 0x60, 0x80},
    {"music,sound,audio,listen,song,tune", "AudioWave", "Music library & streaming", 0x60, 0x80, 0xE0},
    {"music,sound,audio,synth,instrument", "SynthMaster", "Software synthesizer & MIDI", 0xE0, 0x80, 0x60},

    {"browse,web,internet,net,www,online,http", "WebSurf", "Web browser with tabs", 0x40, 0x80, 0xD0},
    {"browse,web,internet,net,online,search", "NetExplorer", "Internet browser & download manager", 0x60, 0xA0, 0xE0},
    {"browse,web,internet,net,page,navigate", "PageView", "Lightweight web viewer", 0x50, 0x70, 0xC0},
    {"browse,web,internet,net,surf,online", "HyperLink", "Web browser with bookmarks", 0x80, 0x60, 0xC0},

    {"mail,email,send,message,inbox,compose", "MailClient", "Email client with IMAP/SMTP", 0x60, 0x80, 0xC0},
    {"mail,email,send,inbox,message,address", "InboxPro", "Email management & organization", 0x80, 0x60, 0xA0},
    {"mail,email,send,compose,message,note", "SendMail", "Simple email sender", 0x60, 0xA0, 0x80},

    {"file,folder,browse,explore,manager,dir", "FileManager", "File browser with copy/move/delete", 0xC0, 0xC0, 0x60},
    {"file,folder,browse,explore,manager,disk", "FileExplorer", "Advanced file management", 0xA0, 0x80, 0x60},
    {"file,folder,browse,explore,zip,archive", "FolderView", "File viewer & archive extractor", 0x80, 0xA0, 0x80},

    {"chat,talk,message,im,conversation,friends", "ChatClient", "Instant messaging & chat", 0x60, 0xC0, 0x80},
    {"chat,talk,message,im,conversation,social", "TalkBox", "Social messaging platform", 0x80, 0x60, 0xC0},
    {"chat,talk,message,im,conference,group", "QuickChat", "Group chat & conferencing", 0x60, 0x80, 0xE0},

    {"video,movie,watch,media,film,cinema", "VideoPlayer", "Video player with codec support", 0xC0, 0x60, 0x60},
    {"video,movie,watch,media,film,stream", "MediaCenter", "Media library & streaming", 0x60, 0x60, 0xC0},
    {"video,movie,watch,film,cinema,theater", "MovieViewer", "Cinema-quality video playback", 0x80, 0x60, 0x80},

    {"code,program,dev,compile,script,develop", "CodeStudio", "IDE with syntax highlighting", 0x40, 0x80, 0x60},
    {"code,program,dev,compile,debug,terminal", "DevTerminal", "Developer terminal & tools", 0x60, 0x60, 0x80},
    {"code,program,dev,compile,script,build", "ScriptRunner", "Script execution & automation", 0x80, 0x80, 0x60},

    {"clock,time,alarm,watch,schedule,calendar", "ClockApp", "Clock, alarms & world time", 0x80, 0x60, 0xA0},
    {"calendar,date,schedule,plan,event,remind", "CalendarPro", "Calendar with reminders", 0xE0, 0x80, 0x60},
    {"weather,forecast,climate,sky,temp,temperature", "WeatherApp", "Weather forecasts & radar", 0x60, 0xA0, 0xE0},

    {"map,navigate,travel,location,geo,address", "MapExplorer", "Interactive maps & navigation", 0x60, 0xC0, 0x60},
    {"camera,photo,picture,snap,capture,image", "CameraApp", "Photo & video capture", 0x80, 0x80, 0x80},
    {"setting,config,pref,options,system,control", "SettingsPanel", "System settings & preferences", 0xA0, 0xA0, 0xA0},
    {"help,info,guide,manual,tutorial,docs", "HelpCenter", "System help & documentation", 0x60, 0x80, 0xA0},

    {"terminal,console,command,shell,prompt,cli", "TerminalPro", "Advanced command-line terminal", 0x40, 0x40, 0x40},
    {"paint,draw,art,image,edit,color,canvas", "PixelPaint", "Raster graphics editor", 0xE0, 0xA0, 0x60},
    {"note,sticky,memo,remind,quick,popup", "StickyNotes", "Desktop sticky notes", 0xE0, 0xE0, 0x60},
};

static int launcher_result_type[LAUNCHER_MAX_RESULTS]; /* 0=installed, 1=ai */
static int launcher_result_idx[LAUNCHER_MAX_RESULTS];

static void draw_outline(int x, int y, int w, int h, uint32_t color)
{
    fb_draw_hline(y, x, x + w - 1, color);
    fb_draw_hline(y + h - 1, x, x + w - 1, color);
    fb_draw_vline(x, y, y + h - 1, color);
    fb_draw_vline(x + w - 1, y, y + h - 1, color);
}

/* Simple keyword matcher — checks if any keyword from comma-separated list matches query */
static int keyword_match(const char* keywords, const char* query)
{
    char kw_copy[128];
    int klen = strlen(keywords);
    if (klen >= 127) klen = 127;
    memcpy(kw_copy, keywords, klen);
    kw_copy[klen] = 0;

    char* token = kw_copy;
    while (*token)
    {
        while (*token == ',' || *token == ' ') token++;
        if (!*token) break;
        char* end = token;
        while (*end && *end != ',') end++;
        char save = *end;
        *end = 0;

        if (token[0] && strstr(query, token))
        {
            *end = save;
            return 1;
        }
        *end = save;
        token = end + 1;
    }
    return 0;
}

static void launcher_toggle_isr(void)
{
    launcher_hotkey_flag = 1;
}

static int launcher_query_groq(const char* query)
{
    if (!query || !query[0]) return 0;

    /* Construct JSON body for Groq API */
    char json_body[512];
    int jlen = 0;
    const char* json_prefix = "{\"model\":\"llama3-8b-8192\",\"messages\":[{\"role\":\"user\",\"content\":\"";
    const char* json_suffix = "\"}],\"max_tokens\":50}";

    const char* p = json_prefix;
    while (*p) json_body[jlen++] = *p++;
    const char* q = query;
    while (*q && jlen < 450)
    {
        if (*q == '"' || *q == '\\') json_body[jlen++] = '\\';
        json_body[jlen++] = *q++;
    }
    p = json_suffix;
    while (*p && jlen < 510) json_body[jlen++] = *p++;
    json_body[jlen] = 0;

    char response[2048];

    /* Try local relay first (host machine proxy, handles TLS) */
    {
        char relay_path[256];
        int rplen = 0;
        const char* rp = "/v1/chat/completions";
        while (*rp) relay_path[rplen++] = *rp++;
        relay_path[rplen] = 0;

        int ret = http_post(GROQ_RELAY_HOST, relay_path,
                            "application/json", json_body, jlen,
                            response, sizeof(response) - 1);
        if (ret > 0)
        {
            /* Find the assistant's reply in JSON response */
            const char* content_start = strstr(response, "\"content\":\"");
            if (content_start)
            {
                content_start += 12;
                const char* content_end = strstr(content_start, "\"");
                if (content_end && content_end > content_start)
                {
                    int clen = content_end - content_start;
                    if (clen > 100) clen = 100;
                    memcpy(ai_generating_msg, "AI: ", 4);
                    memcpy(ai_generating_msg + 4, content_start, clen);
                    ai_generating_msg[4 + clen] = 0;
                    return 1;
                }
            }
        }
    }

    /* Fallback: try direct Groq API (requires TLS, will likely fail) */
    {
        char groq_path[256];
        int gplen = 0;
        const char* gp = "/openai/v1/chat/completions";
        while (*gp) groq_path[gplen++] = *gp++;
        groq_path[gplen] = 0;

        int ret = http_post(GROQ_API_HOST, groq_path,
                            "application/json", json_body, jlen,
                            response, sizeof(response) - 1);
        if (ret > 0)
        {
            const char* content_start = strstr(response, "\"content\":\"");
            if (content_start)
            {
                content_start += 12;
                const char* content_end = strstr(content_start, "\"");
                if (content_end && content_end > content_start)
                {
                    int clen = content_end - content_start;
                    if (clen > 100) clen = 100;
                    memcpy(ai_generating_msg, "AI: ", 4);
                    memcpy(ai_generating_msg + 4, content_start, clen);
                    ai_generating_msg[4 + clen] = 0;
                    return 1;
                }
            }
        }
    }

    return 0;
}

void launcher_init(void)
{
    kernel_api.register_hotkey_callback(launcher_toggle_isr);
    ai_generating_msg[0] = 0;
}

/* Update search results: installed programs + AI suggestions */
static void launcher_update_results(void)
{
    int installed_count = 0;

    /* Match installed programs */
    if (search_pos == 0)
    {
        for (int i = 0; i < num_launcher_apps && installed_count < LAUNCHER_MAX_RESULTS; i++)
        {
            launcher_result_type[installed_count] = 0;
            launcher_result_idx[installed_count] = i;
            installed_count++;
        }
    }
    else
    {
        for (int i = 0; i < num_launcher_apps && installed_count < LAUNCHER_MAX_RESULTS; i++)
        {
            if (strstr(launcher_apps[i].name, search_text))
            {
                launcher_result_type[installed_count] = 0;
                launcher_result_idx[installed_count] = i;
                installed_count++;
            }
        }
    }

    /* Fill remaining slots with AI-generated suggestions */
    int ai_count = 0;
    int remaining = LAUNCHER_MAX_RESULTS - installed_count;

    for (int i = 0; i < AI_DB_SIZE && ai_count < remaining && ai_count < LAUNCHER_AI_RESULTS; i++)
    {
        if (search_pos == 0 || keyword_match(ai_db[i].keywords, search_text))
        {
            /* Avoid duplicate names */
            int dup = 0;
            for (int j = 0; j < ai_count; j++)
            {
                if (strcmp(ai_db[launcher_result_idx[installed_count + j]].app_name, ai_db[i].app_name) == 0)
                { dup = 1; break; }
            }
            if (!dup)
            {
                launcher_result_type[installed_count + ai_count] = 1;
                launcher_result_idx[installed_count + ai_count] = i;
                ai_count++;
            }
        }
    }

    num_results = installed_count + ai_count;

    /* Reset selection if out of bounds */
    if (selected_result >= num_results) selected_result = 0;
    if (selected_result < 0) selected_result = 0;
}

static void launcher_process_key(char c)
{
    if (c == '\033')
    {
        launcher_open = 0;
        search_pos = 0;
        search_text[0] = 0;
        gui_mark_dirty(0, 0, fb_info.width, fb_info.height);
        return;
    }

    if (c == '\n')
    {
        if (num_results > 0 && selected_result >= 0 && selected_result < num_results)
        {
            int type = launcher_result_type[selected_result];
            int idx = launcher_result_idx[selected_result];

            if (type == 0 && idx >= 0 && idx < num_launcher_apps && launcher_apps[idx].path)
            {
                launcher_open = 0;
                search_pos = 0;
                search_text[0] = 0;
                gui_mark_dirty(0, 0, fb_info.width, fb_info.height);
                blu_spawn(launcher_apps[idx].path);
            }
            else if (type == 1 && idx >= 0 && idx < AI_DB_SIZE)
            {
                /* Try real Groq API, fall back to local message */
                if (!launcher_query_groq(search_text))
                {
                    const char* name = ai_db[idx].app_name;
                    int slen = strlen(name);
                    if (slen > 50) slen = 50;
                    memcpy(ai_generating_msg, "AI generating ", 13);
                    memcpy(ai_generating_msg + 13, name, slen);
                    ai_generating_msg[13 + slen] = 0;
                }
            }
        }
        return;
    }

    if (c == '\b')
    {
        if (search_pos > 0)
        {
            search_pos--;
            search_text[search_pos] = 0;
        }
        ai_generating_msg[0] = 0;
        gui_mark_dirty(0, 0, fb_info.width, fb_info.height);
        return;
    }

    if (search_pos < LAUNCHER_SEARCH_LEN - 1 && c >= ' ')
    {
        search_text[search_pos++] = c;
        search_text[search_pos] = 0;
        selected_result = 0;
        ai_generating_msg[0] = 0;
        gui_mark_dirty(0, 0, fb_info.width, fb_info.height);
    }
}

void launcher_update(void)
{
    if (launcher_hotkey_flag)
    {
        launcher_hotkey_flag = 0;
        launcher_open = !launcher_open;
        if (launcher_open)
        {
            search_pos = 0;
            search_text[0] = 0;
            selected_result = 0;
            ai_generating_msg[0] = 0;
        }
        gui_mark_dirty(0, 0, fb_info.width, fb_info.height);
    }

    if (launcher_open)
    {
        while (keyb_char_avail_wrapper())
        {
            char c = keyb_getchar_wrapper();
            if (c)
                launcher_process_key(c);
        }
    }
}

static int launcher_get_result_at(int mx, int my)
{
    if (num_results <= 0) return -1;
    if (mx < bar_x || mx >= bar_x + bar_w) return -1;
    int list_y = bar_y + bar_h + 4;
    if (my < list_y || my >= list_y + num_results * item_h) return -1;
    return (my - list_y) / item_h;
}

void launcher_handle_click(int mx, int my)
{
    if (!launcher_open) return;

    int idx = launcher_get_result_at(mx, my);
    if (idx >= 0 && idx < num_results)
    {
        int type = launcher_result_type[idx];
        int app_idx = launcher_result_idx[idx];
        if (type == 0 && app_idx >= 0 && app_idx < num_launcher_apps && launcher_apps[app_idx].path)
        {
            launcher_open = 0;
            search_pos = 0;
            search_text[0] = 0;
            gui_mark_dirty(0, 0, fb_info.width, fb_info.height);
            blu_spawn(launcher_apps[app_idx].path);
        }
        else if (type == 1 && app_idx >= 0 && app_idx < AI_DB_SIZE)
        {
            if (!launcher_query_groq(search_text))
            {
                const char* name = ai_db[app_idx].app_name;
                int slen = strlen(name);
                if (slen > 50) slen = 50;
                memcpy(ai_generating_msg, "AI generating ", 13);
                memcpy(ai_generating_msg + 13, name, slen);
                ai_generating_msg[13 + slen] = 0;
            }
        }
        return;
    }

    if (mx >= bar_x && mx < bar_x + bar_w && my >= bar_y && my < bar_y + bar_h)
        return;

    launcher_open = 0;
    search_pos = 0;
    search_text[0] = 0;
    ai_generating_msg[0] = 0;
    gui_mark_dirty(0, 0, fb_info.width, fb_info.height);
}

void launcher_render(void)
{
    if (!launcher_open) return;

    launcher_update_results();

    int sw = fb_info.width;
    int sh = fb_info.height;

    /* Semi-transparent backdrop */
    fb_fillrect_alpha(0, 0, sw, sh, COL_BLACK, 80);

    bar_x = (sw - bar_w) / 2;
    bar_y = sh / 5;

    /* Search bar */
    fb_fillrect(bar_x, bar_y, bar_w, bar_h, COL_WHITE);
    draw_outline(bar_x, bar_y, bar_w, bar_h, FB_RGB(0x00, 0x58, 0xEE));

    int tx = bar_x + 8;
    int ty = bar_y + (bar_h - FONT_HEIGHT) / 2;

    /* Draw search icon */
    fb_drawchar(tx, ty, '>', FB_RGB(0x00, 0x80, 0x00), COL_WHITE);
    tx += FONT_WIDTH * 2;

    /* Draw input text */
    for (int i = 0; i < search_pos; i++)
    {
        fb_drawchar(tx, ty, search_text[i], COL_BLACK, COL_WHITE);
        tx += FONT_WIDTH;
    }

    /* Blinking cursor */
    {
        uint32_t* bb = fb_get_backbuffer();
        if (bb)
        {
            uint32_t stride = fb_info.pitch / 4;
            for (int row = 0; row < FONT_HEIGHT; row++)
            {
                int py = ty + row;
                if (py < 0 || (uint32_t)py >= sh) continue;
                for (int col = 0; col < FONT_WIDTH; col++)
                {
                    int px = tx + col;
                    if (px < 0 || (uint32_t)px >= sw) continue;
                    bb[py * stride + px] = ~bb[py * stride + px] & 0x00FFFFFF;
                }
            }
        }
    }

    /* AI generating message */
    if (ai_generating_msg[0])
    {
        int msg_y = bar_y + bar_h + 8;
        fb_fillrect(bar_x, msg_y, bar_w, item_h, FB_RGB(0xFF, 0xF0, 0xE0));
        draw_outline(bar_x, msg_y, bar_w, item_h, FB_RGB(0xCC, 0x88, 0x44));
        fb_drawstring(bar_x + 8, msg_y + (item_h - FONT_HEIGHT) / 2, ai_generating_msg, FB_RGB(0x88, 0x44, 0x00), FB_RGB(0xFF, 0xF0, 0xE0));
        return;
    }

    /* Results list */
    if (num_results > 0)
    {
        int list_y = bar_y + bar_h + 4;
        int list_h = num_results * item_h;

        fb_fillrect(bar_x, list_y, bar_w, list_h, COL_WHITE);
        draw_outline(bar_x, list_y, bar_w, list_h, FB_RGB(0x7F, 0x9D, 0xB9));

        int mmx = mouse_get_x_wrapper();
        int mmy = mouse_get_y_wrapper();
        int hover_idx = launcher_get_result_at(mmx, mmy);
        if (hover_idx >= 0 && hover_idx < num_results)
            selected_result = hover_idx;

        for (int i = 0; i < num_results; i++)
        {
            int type = launcher_result_type[i];
            int app_idx = launcher_result_idx[i];
            int iy = list_y + i * item_h;
            int is_selected = (i == selected_result);
            uint32_t bg, fg;

            if (is_selected)
            {
                bg = type == 0 ? FB_RGB(0x31, 0x6A, 0xC5) : FB_RGB(0x70, 0x90, 0x30);
                fg = COL_WHITE;
            }
            else
            {
                bg = COL_WHITE;
                fg = COL_BLACK;
            }

            fb_fillrect(bar_x + 2, iy, bar_w - 4, item_h, bg);

            if (type == 1)
            {
                /* AI suggestion — show small sparkle icon */
                const ai_suggestion_t* ai = &ai_db[app_idx];
                int icon_x = bar_x + 6;
                int icon_y = iy + (item_h - 12) / 2;
                fb_fillrect(icon_x, icon_y, 12, 12, FB_RGB(ai->icon_r, ai->icon_g, ai->icon_b));
                fb_drawstring(bar_x + 22, iy + (item_h - FONT_HEIGHT) / 2, ai->app_name, fg, bg);
            }
            else
            {
                /* Installed program */
                int icon_x = bar_x + 6;
                int icon_y = iy + (item_h - 14) / 2;
                fb_fillrect(icon_x, icon_y, 14, 14, FB_RGB(0x30, 0x80, 0xD0));
                draw_outline(icon_x, icon_y, 14, 14, FB_RGB(0x20, 0x60, 0xB0));

                int tx2 = bar_x + 24;
                char label[64];
                const char* name = launcher_apps[app_idx].name;
                int nl = strlen(name);
                if (nl > 50) nl = 50;
                memcpy(label, name, nl);
                label[nl] = 0;
                fb_drawstring(tx2, iy + (item_h - FONT_HEIGHT) / 2, label, fg, bg);
            }
        }
    }
    else if (search_pos > 0)
    {
        int list_y = bar_y + bar_h + 4;
        fb_fillrect(bar_x, list_y, bar_w, 60, COL_WHITE);
        draw_outline(bar_x, list_y, bar_w, 60, FB_RGB(0x7F, 0x9D, 0xB9));
        fb_drawstring(bar_x + 8, list_y + 8, "Searching AI knowledge base...", FB_RGB(0x80, 0x80, 0x80), COL_WHITE);
    }
}

int launcher_is_open(void)
{
    return launcher_open;
}

/*
 * Codex95 GUI client
 *
 * Target: Visual C++ 6.0 / Windows 95 / Winsock 1.1
 * Build: cl /O1 /W3 codex95_gui.c wsock32.lib shell32.lib /link /subsystem:windows
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <process.h>

#define APP_TITLE "Codex95"
#define BUF_SIZE 4096
#define BIG_SIZE 49152
#define RESULT_SIZE 32000
#define TRANSCRIPT_LIMIT 28000
#define TRANSCRIPT_TRIM 8000

#define IDC_TRANSCRIPT 101
#define IDC_PROMPT 102
#define IDC_SEND 103
#define IDC_PROJECT 104
#define IDC_BROWSE 105
#define IDC_AUTO 106
#define IDC_STATUS 107
#define IDC_HOST 108
#define IDC_PROJECTS 109
#define IDC_NEW_PROJECT 110
#define IDC_REFRESH_PROJECTS 111
#define IDC_DELETE_PROJECT 112
#define IDC_RENAME_PROJECT 113
#define IDM_SETTINGS 201
#define IDM_UPDATE_CLIENT 202
#define IDC_SET_HOST 301
#define IDC_SET_DEVICE 302
#define IDC_SET_AUTO 303
#define IDC_SET_FULL 304
#define IDC_SET_OK 305
#define IDC_SET_CANCEL 306
#define IDC_SET_DARK 307
#define IDC_SET_MODEL 308
#define IDC_SET_SCALE 309
#define IDC_NAME_EDIT 401
#define IDC_NAME_OK 402
#define IDC_NAME_CANCEL 403

#define WM_APPEND_TEXT (WM_USER + 1)
#define WM_TASK_DONE (WM_USER + 2)
#define WM_SET_STATUS (WM_USER + 3)
#define WM_RUN_SMOKE (WM_USER + 4)

typedef struct {
    char host[128];
    int port;
    char root[MAX_PATH];
    char device_name[128];
    char model[80];
    int automatic;
    int full_access;
    int dark_mode;
    int text_scale;
} Config;

typedef struct {
    char prompt[BUF_SIZE];
    Config cfg;
} Task;

static HWND g_main;
static HWND g_transcript;
static HWND g_prompt;
static HWND g_send;
static HWND g_project;
static HWND g_auto;
static HWND g_status;
static HWND g_host;
static HWND g_projects;
static Config g_settings_cfg;
static int g_settings_done;
static int g_dark_mode;
static HBRUSH g_dark_brush;
static HBRUSH g_dark_edit_brush;
static HFONT g_ui_font;
static int g_text_scale;
static char g_ini[MAX_PATH];
static volatile int g_busy = 0;
static int g_smoke = 0;
static char g_name_value[MAX_PATH];
static int g_name_done;

static void controls_to_config(Config *cfg);
static void ensure_directory_tree(const char *path);

static HFONT make_ui_font(int scale) {
    int height = scale <= 0 ? 13 : (scale == 1 ? 15 : (scale == 2 ? 17 : (scale == 3 ? 20 : 23)));
    return CreateFont(-height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, FF_DONTCARE, "MS Sans Serif");
}

static void set_font_if(HWND hwnd, HFONT font) {
    if (hwnd && font) SendMessage(hwnd, WM_SETFONT, (WPARAM)font, TRUE);
}

static const char b64_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

static int starts_with_ci(const char *s, const char *prefix) {
    while (*prefix) {
        if (tolower((unsigned char)*s++) != tolower((unsigned char)*prefix++))
            return 0;
    }
    return 1;
}

static void url_encode(const char *src, char *dst, size_t cap) {
    static const char hex[] = "0123456789ABCDEF";
    size_t used = 0;
    while (*src && used + 4 < cap) {
        unsigned char c = (unsigned char)*src++;
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            dst[used++] = (char)c;
        } else if (c == ' ') {
            dst[used++] = '+';
        } else {
            dst[used++] = '%';
            dst[used++] = hex[c >> 4];
            dst[used++] = hex[c & 15];
        }
    }
    dst[used] = 0;
}

static int b64_value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+' || c == '-') return 62;
    if (c == '/' || c == '_') return 63;
    return -1;
}

static void b64_encode(const unsigned char *src, size_t len, char *dst, size_t cap) {
    size_t i = 0, o = 0;
    while (i < len && o + 5 < cap) {
        size_t chunk = len - i;
        unsigned long v = src[i++] << 16;
        if (chunk > 1) v |= src[i++] << 8;
        if (chunk > 2) v |= src[i++];
        dst[o++] = b64_table[(v >> 18) & 63];
        dst[o++] = b64_table[(v >> 12) & 63];
        if (chunk > 1) dst[o++] = b64_table[(v >> 6) & 63];
        if (chunk > 2) dst[o++] = b64_table[v & 63];
    }
    dst[o] = 0;
}

static int b64_decode(const char *src, unsigned char *dst, size_t cap) {
    unsigned long val = 0;
    int bits = -8;
    size_t o = 0;
    while (*src) {
        int c;
        if (*src == '=') break;
        c = b64_value(*src++);
        if (c < 0) continue;
        val = (val << 6) | c;
        bits += 6;
        if (bits >= 0) {
            if (o + 1 >= cap) break;
            dst[o++] = (unsigned char)((val >> bits) & 255);
            bits -= 8;
        }
    }
    dst[o] = 0;
    return (int)o;
}

static const char *field(const char *text, const char *name, char *out, size_t cap) {
    size_t n = strlen(name);
    const char *p = text;
    while (*p) {
        const char *end = strstr(p, "\n");
        size_t line_len = end ? (size_t)(end - p) : strlen(p);
        if (line_len > n + 1 && !strncmp(p, name, n) && p[n] == '=') {
            size_t copy = line_len - n - 1;
            if (copy >= cap) copy = cap - 1;
            memcpy(out, p + n + 1, copy);
            out[copy] = 0;
            if (copy && out[copy - 1] == '\r') out[copy - 1] = 0;
            return out;
        }
        if (!end) break;
        p = end + 1;
    }
    out[0] = 0;
    return out;
}

static void post_alloc(UINT msg, const char *text) {
    const char *p;
    char *copy, *d;
    size_t extra = 0;
    for (p = text; *p; p++) {
        if (*p == '\n' && (p == text || p[-1] != '\r')) extra++;
    }
    copy = (char *)malloc(strlen(text) + extra + 1);
    if (!copy) return;
    d = copy;
    for (p = text; *p; p++) {
        if (*p == '\n' && (p == text || p[-1] != '\r')) *d++ = '\r';
        *d++ = *p;
    }
    *d = 0;
    if (!PostMessage(g_main, msg, 0, (LPARAM)copy)) free(copy);
}

static HBRUSH control_color(HDC dc, int edit) {
    if (!g_dark_mode) return NULL;
    SetTextColor(dc, RGB(230, 230, 230));
    SetBkColor(dc, edit ? RGB(32, 34, 38) : RGB(45, 47, 52));
    SetBkMode(dc, TRANSPARENT);
    return edit ? g_dark_edit_brush : g_dark_brush;
}

static void apply_theme(void) {
    HWND settings = FindWindow("Codex95Settings", NULL);
    HFONT old = g_ui_font;
    g_ui_font = make_ui_font(g_text_scale);
    set_font_if(g_transcript, g_ui_font);
    set_font_if(g_prompt, g_ui_font);
    set_font_if(g_project, g_ui_font);
    set_font_if(g_host, g_ui_font);
    set_font_if(g_send, g_ui_font);
    set_font_if(g_auto, g_ui_font);
    set_font_if(g_status, g_ui_font);
    set_font_if(g_projects, g_ui_font);
    if (old) DeleteObject(old);
    InvalidateRect(g_main, NULL, TRUE);
    if (settings) InvalidateRect(settings, NULL, TRUE);
}

static void log_action(const char *action, const char *detail) {
    char text[BUF_SIZE];
    if (detail && detail[0])
        _snprintf(text, sizeof(text) - 1, "\r\n[%s] %s\r\n", action, detail);
    else
        _snprintf(text, sizeof(text) - 1, "\r\n[%s]\r\n", action);
    text[sizeof(text) - 1] = 0;
    post_alloc(WM_APPEND_TEXT, text);
}

static int resolve_path(const Config *cfg, const char *relative, char *out) {
    char combined[MAX_PATH * 2];
    char root[MAX_PATH];
    size_t root_len;
    if (!GetFullPathName(cfg->root, MAX_PATH, root, NULL)) return 0;
    if (cfg->full_access && ((relative[0] && relative[1] == ':') ||
        (relative[0] == '\\' && relative[1] == '\\')))
        return GetFullPathName(relative, MAX_PATH, out, NULL) != 0;
    if (_snprintf(combined, sizeof(combined) - 1, "%s\\%s", root, relative) < 0)
        return 0;
    combined[sizeof(combined) - 1] = 0;
    if (!GetFullPathName(combined, MAX_PATH, out, NULL)) return 0;
    root_len = strlen(root);
    if (!starts_with_ci(out, root)) return 0;
    return out[root_len] == 0 || out[root_len] == '\\';
}

static int delete_tree(const char *path) {
    WIN32_FIND_DATA fd;
    HANDLE find;
    char pattern[MAX_PATH], child[MAX_PATH];
    DWORD attrs = GetFileAttributes(path);
    if (attrs == 0xFFFFFFFF) return 0;
    if (!(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        SetFileAttributes(path, FILE_ATTRIBUTE_NORMAL);
        return DeleteFile(path) != 0;
    }
    _snprintf(pattern, sizeof(pattern) - 1, "%s\\*.*", path);
    pattern[sizeof(pattern) - 1] = 0;
    find = FindFirstFile(pattern, &fd);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, "..")) continue;
            _snprintf(child, sizeof(child) - 1, "%s\\%s", path, fd.cFileName);
            child[sizeof(child) - 1] = 0;
            if (!delete_tree(child)) { FindClose(find); return 0; }
        } while (FindNextFile(find, &fd));
        FindClose(find);
    }
    SetFileAttributes(path, FILE_ATTRIBUTE_NORMAL);
    return RemoveDirectory(path) != 0;
}

static int approve(const Config *cfg, const char *action, const char *detail) {
    char text[BUF_SIZE];
    if (cfg->automatic) return 1;
    _snprintf(text, sizeof(text) - 1, "Allow %s?\r\n\r\n%s", action, detail);
    text[sizeof(text) - 1] = 0;
    return MessageBox(g_main, text, APP_TITLE, MB_YESNO | MB_ICONQUESTION) == IDYES;
}

static int send_all(SOCKET sock, const char *data, int length) {
    int sent, total = 0;
    while (total < length) {
        sent = send(sock, data + total, length - total, 0);
        if (sent == SOCKET_ERROR || sent == 0) return 0;
        total += sent;
    }
    return 1;
}

static int http_post(const Config *cfg, const char *path, const char *body,
                     char *response, size_t response_cap) {
    struct hostent *he;
    struct sockaddr_in addr;
    SOCKET sock;
    char header[1024];
    char raw[BIG_SIZE];
    int got, total = 0, body_len;
    char *payload;
    unsigned long numeric_addr;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short)cfg->port);
    numeric_addr = inet_addr(cfg->host);
    if (numeric_addr != INADDR_NONE) {
        addr.sin_addr.s_addr = numeric_addr;
    } else {
        he = gethostbyname(cfg->host);
        if (!he) return 0;
        memcpy(&addr.sin_addr, he->h_addr, he->h_length);
    }
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) return 0;
    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR) {
        closesocket(sock);
        return 0;
    }
    sprintf(header,
        "POST %s HTTP/1.0\r\nHost: %s\r\nContent-Type: application/x-www-form-urlencoded\r\n"
        "Content-Length: %lu\r\nConnection: close\r\n\r\n",
        path, cfg->host, (unsigned long)strlen(body));
    if (!send_all(sock, header, (int)strlen(header))) { closesocket(sock); return 0; }
    body_len = (int)strlen(body);
    if (!send_all(sock, body, body_len)) { closesocket(sock); return 0; }
    while ((got = recv(sock, raw + total, sizeof(raw) - total - 1, 0)) > 0) {
        total += got;
        if (total >= (int)sizeof(raw) - 1) break;
    }
    closesocket(sock);
    raw[total] = 0;
    payload = strstr(raw, "\r\n\r\n");
    if (!payload) return 0;
    payload += 4;
    strncpy(response, payload, response_cap - 1);
    response[response_cap - 1] = 0;
    return 1;
}

static int http_get_file(const Config *cfg, const char *path, const char *file_path) {
    struct hostent *he;
    struct sockaddr_in addr;
    SOCKET sock;
    HANDLE out;
    char header[512], raw[2048], *payload;
    int got, header_done = 0, ok = 0;
    DWORD written;
    unsigned long numeric_addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short)cfg->port);
    numeric_addr = inet_addr(cfg->host);
    if (numeric_addr != INADDR_NONE) addr.sin_addr.s_addr = numeric_addr;
    else {
        he = gethostbyname(cfg->host);
        if (!he) return 0;
        memcpy(&addr.sin_addr, he->h_addr, he->h_length);
    }
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) return 0;
    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR) {
        closesocket(sock);
        return 0;
    }
    sprintf(header, "GET %s HTTP/1.0\r\nHost: %s\r\nConnection: close\r\n\r\n", path, cfg->host);
    if (!send_all(sock, header, (int)strlen(header))) { closesocket(sock); return 0; }
    out = INVALID_HANDLE_VALUE;
    while ((got = recv(sock, raw, sizeof(raw) - 1, 0)) > 0) {
        raw[got] = 0;
        if (!header_done) {
            payload = strstr(raw, "\r\n\r\n");
            if (!payload) continue;
            ok = !strncmp(raw, "HTTP/1.0 200", 12) || !strncmp(raw, "HTTP/1.1 200", 12);
            if (!ok) break;
            out = CreateFile(file_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            if (out == INVALID_HANDLE_VALUE) break;
            header_done = 1;
            payload += 4;
            WriteFile(out, payload, got - (int)(payload - raw), &written, NULL);
        } else {
            WriteFile(out, raw, got, &written, NULL);
        }
    }
    if (out != INVALID_HANDLE_VALUE) CloseHandle(out);
    closesocket(sock);
    if (!header_done || !ok) DeleteFile(file_path);
    return header_done && ok;
}

static void append_connect_error(const Config *cfg) {
    char text[256];
    _snprintf(text, sizeof(text) - 1,
        "\r\n[error] Cannot connect to bridge at %s:%d.\r\n",
        cfg->host, cfg->port);
    text[sizeof(text) - 1] = 0;
    post_alloc(WM_APPEND_TEXT, text);
}

static int discover_bridge(Config *cfg) {
    SOCKET sock;
    struct sockaddr_in broadcast, from;
    int yes = 1, from_len = sizeof(from);
    char request[] = "CODEX95_DISCOVER";
    char reply[128];
    int got;
    struct timeval timeout;
    fd_set read_set;

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == INVALID_SOCKET) return 0;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, (char *)&yes, sizeof(yes));
    memset(&broadcast, 0, sizeof(broadcast));
    broadcast.sin_family = AF_INET;
    broadcast.sin_port = htons(8788);
    broadcast.sin_addr.s_addr = INADDR_BROADCAST;
    sendto(sock, request, (int)strlen(request), 0,
        (struct sockaddr *)&broadcast, sizeof(broadcast));
    FD_ZERO(&read_set);
    FD_SET(sock, &read_set);
    timeout.tv_sec = 2;
    timeout.tv_usec = 0;
    if (select(0, &read_set, NULL, NULL, &timeout) <= 0) {
        closesocket(sock);
        return 0;
    }
    got = recvfrom(sock, reply, sizeof(reply) - 1, 0,
        (struct sockaddr *)&from, &from_len);
    closesocket(sock);
    if (got <= 0) return 0;
    reply[got] = 0;
    if (strncmp(reply, "CODEX95_BRIDGE ", 15)) return 0;
    strncpy(cfg->host, inet_ntoa(from.sin_addr), sizeof(cfg->host) - 1);
    cfg->host[sizeof(cfg->host) - 1] = 0;
    cfg->port = atoi(reply + 15);
    if (!cfg->port) cfg->port = 8787;
    return 1;
}

static void list_dir(const char *path, char *out, size_t cap) {
    WIN32_FIND_DATA fd;
    HANDLE h;
    char pattern[MAX_PATH];
    size_t used = 0;
    if (_snprintf(pattern, sizeof(pattern) - 1, "%s\\*.*", path) < 0) {
        strcpy(out, "ERROR: path is too long");
        return;
    }
    pattern[sizeof(pattern) - 1] = 0;
    h = FindFirstFile(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) { strcpy(out, "ERROR: directory not found"); return; }
    do {
        int n;
        if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, "..")) continue;
        n = _snprintf(out + used, cap - used - 1, "%s\t%s\r\n",
            (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? "DIR" : "FILE",
            fd.cFileName);
        if (n < 0) break;
        used += n;
    } while (FindNextFile(h, &fd) && used + 64 < cap);
    FindClose(h);
    out[used] = 0;
}

static void read_file(const char *path, char *out, size_t cap) {
    FILE *f = fopen(path, "rb");
    size_t n;
    if (!f) { strcpy(out, "ERROR: cannot open file"); return; }
    n = fread(out, 1, cap - 1, f);
    out[n] = 0;
    fclose(f);
}

static void write_file(const char *path, const unsigned char *data, int len, char *out) {
    FILE *f = fopen(path, "wb");
    if (!f) { strcpy(out, "ERROR: cannot create file"); return; }
    if ((int)fwrite(data, 1, len, f) != len) strcpy(out, "ERROR: short write");
    else sprintf(out, "OK: wrote %d bytes", len);
    fclose(f);
}

static void make_dir(const char *path, char *out) {
    if (CreateDirectory(path, NULL) || GetLastError() == ERROR_ALREADY_EXISTS)
        strcpy(out, "OK");
    else
        sprintf(out, "ERROR: CreateDirectory failed (%lu)", GetLastError());
}

static void run_command(const Config *cfg, const char *command, char *out, size_t cap) {
    char temp[MAX_PATH], cmd[BIG_SIZE], full_temp[MAX_PATH], shell[MAX_PATH];
    STARTUPINFO si;
    PROCESS_INFORMATION pi;
    SECURITY_ATTRIBUTES sa;
    HANDLE output;
    DWORD code = 0, wait;
    FILE *f;
    size_t n;

    if (!GetTempPath(MAX_PATH, temp) || !GetTempFileName(temp, "C95", 0, full_temp)) {
        strcpy(out, "ERROR: cannot create temporary output file");
        return;
    }
    if (!GetEnvironmentVariable("COMSPEC", shell, sizeof(shell)))
        strcpy(shell, "COMMAND.COM");
    if (_snprintf(cmd, sizeof(cmd) - 1, "\"%s\" /C %s", shell, command) < 0) {
        strcpy(out, "ERROR: command is too long");
        DeleteFile(full_temp);
        return;
    }
    cmd[sizeof(cmd) - 1] = 0;
    memset(&sa, 0, sizeof(sa));
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    output = CreateFile(full_temp, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
        &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (output == INVALID_HANDLE_VALUE) {
        strcpy(out, "ERROR: cannot open temporary output file");
        DeleteFile(full_temp);
        return;
    }
    memset(&si, 0, sizeof(si));
    memset(&pi, 0, sizeof(pi));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = output;
    si.hStdError = output;
    if (!CreateProcess(NULL, cmd, NULL, NULL, TRUE, 0, NULL, cfg->root, &si, &pi)) {
        sprintf(out, "ERROR: CreateProcess failed (%lu)", GetLastError());
        CloseHandle(output);
        DeleteFile(full_temp);
        return;
    }
    wait = WaitForSingleObject(pi.hProcess, 120000);
    if (wait == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, 5000);
        strcpy(out, "ERROR: command timed out after 120 seconds");
    } else {
        GetExitCodeProcess(pi.hProcess, &code);
    }
    CloseHandle(output);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    if (wait != WAIT_TIMEOUT) {
        f = fopen(full_temp, "rb");
        if (f) {
            n = fread(out, 1, cap - 64, f);
            fclose(f);
            out[n] = 0;
            sprintf(out + n, "\r\n[exit=%lu]", code);
        } else {
            sprintf(out, "Exit code: %lu", code);
        }
    }
    DeleteFile(full_temp);
}

static void run_program(const Config *cfg, const char *command, char *out) {
    STARTUPINFO si;
    PROCESS_INFORMATION pi;
    char mutable_cmd[BUF_SIZE];
    char program[MAX_PATH], full[MAX_PATH];
    const char *p = command;
    char *d = program;
    int quoted = 0;
    DWORD attrs;
    if (!command[0]) {
        strcpy(out, "ERROR: empty program command");
        return;
    }
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '"') { quoted = 1; p++; }
    while (*p && d < program + sizeof(program) - 1) {
        if ((quoted && *p == '"') || (!quoted && (*p == ' ' || *p == '\t'))) break;
        *d++ = *p++;
    }
    *d = 0;
    if (!resolve_path(cfg, program, full)) {
        strcpy(out, "ERROR: program must exist inside the project directory");
        return;
    }
    attrs = GetFileAttributes(full);
    if (attrs == 0xFFFFFFFF || (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        strcpy(out, "ERROR: program must exist inside the project directory");
        return;
    }
    strncpy(mutable_cmd, command, sizeof(mutable_cmd) - 1);
    mutable_cmd[sizeof(mutable_cmd) - 1] = 0;
    memset(&si, 0, sizeof(si));
    memset(&pi, 0, sizeof(pi));
    si.cb = sizeof(si);
    if (!CreateProcess(full, mutable_cmd, NULL, NULL, FALSE, 0, NULL, cfg->root, &si, &pi)) {
        sprintf(out, "ERROR: could not launch program (%lu)", GetLastError());
        return;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    strcpy(out, "OK: program launched");
}

static void profile_append(char *out, size_t cap, const char *line) {
    size_t used = strlen(out);
    if (used + strlen(line) + 1 < cap) strcat(out, line);
}

static void device_profile(const Config *cfg, char *out, size_t cap) {
    OSVERSIONINFO os;
    SYSTEM_INFO sys;
    MEMORYSTATUS mem;
    char computer[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD computer_len = sizeof(computer);
    DWORD sectors, bytes, free_clusters, total_clusters;
    HDC dc;
    int depth = 0;
    const char *cpu;
    const char *tools[] = { "CL.EXE", "BCC32.EXE", "BCC.EXE", "TCC.EXE",
                            "GCC.EXE", "NMAKE.EXE", "MAKE.EXE", "RC.EXE" };
    const char *common[] = {
        "C:\\BORLANDC\\BIN\\BCC.EXE", "C:\\BORLANDC\\BIN\\BCC32.EXE",
        "C:\\BC5\\BIN\\BCC32.EXE", "C:\\TC\\BIN\\TCC.EXE",
        "C:\\MSVC\\BIN\\CL.EXE", "C:\\VC98\\BIN\\CL.EXE"
    };
    char found[MAX_PATH];
    char line[MAX_PATH + 32];
    size_t used;
    int i, found_count = 0;
    memset(&os, 0, sizeof(os));
    os.dwOSVersionInfoSize = sizeof(os);
    GetVersionEx(&os);
    GetSystemInfo(&sys);
    mem.dwLength = sizeof(mem);
    GlobalMemoryStatus(&mem);
    if (!GetComputerName(computer, &computer_len)) strcpy(computer, "unknown");
    switch (sys.dwProcessorType) {
        case 386: cpu = "Intel 386 compatible"; break;
        case 486: cpu = "Intel 486 compatible"; break;
        case 586: cpu = "Intel Pentium compatible"; break;
        default: cpu = "x86 compatible"; break;
    }
    dc = GetDC(NULL);
    if (dc) {
        depth = GetDeviceCaps(dc, BITSPIXEL) * GetDeviceCaps(dc, PLANES);
        ReleaseDC(NULL, dc);
    }
    _snprintf(out, cap - 1,
        "Device: %s\r\nComputer name: %s\r\n"
        "OS: Windows %lu.%lu build %lu (Win32 platform %lu)\r\n"
        "CPU: %s, %lu processor(s)\r\n"
        "RAM: %lu MB total, %lu MB available\r\n"
        "Screen: %dx%d, %d-bit color\r\n"
        "Codepages: ANSI %u, OEM %u\r\n"
        "Access mode: %s\r\n"
        "Project root: %s\r\n",
        cfg->device_name, computer,
        os.dwMajorVersion, os.dwMinorVersion, os.dwBuildNumber, os.dwPlatformId,
        cpu, sys.dwNumberOfProcessors,
        mem.dwTotalPhys / (1024UL * 1024UL), mem.dwAvailPhys / (1024UL * 1024UL),
        GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), depth,
        GetACP(), GetOEMCP(), cfg->full_access ? "full computer" : "project only", cfg->root);
    out[cap - 1] = 0;
    if (cfg->root[0] && cfg->root[1] == ':' &&
        GetDiskFreeSpace(cfg->root, &sectors, &bytes, &free_clusters, &total_clusters)) {
        _snprintf(line, sizeof(line) - 1, "Project disk: %.1f MB free of %.1f MB\r\n",
            (double)free_clusters * sectors * bytes / (1024.0 * 1024.0),
            (double)total_clusters * sectors * bytes / (1024.0 * 1024.0));
        line[sizeof(line) - 1] = 0;
        profile_append(out, cap, line);
    }
    profile_append(out, cap, "Detected build tools:\r\n");
    used = strlen(out);
    for (i = 0; i < (int)(sizeof(tools) / sizeof(tools[0])); i++) {
        if (SearchPath(NULL, tools[i], NULL, MAX_PATH, found, NULL)) {
            found_count++;
            _snprintf(line, sizeof(line) - 1, "%s=%s\r\n", tools[i], found);
            line[sizeof(line) - 1] = 0;
            if (used + strlen(line) + 1 < cap) {
                strcat(out, line);
                used += strlen(line);
            }
        }
    }
    for (i = 0; i < (int)(sizeof(common) / sizeof(common[0])); i++) {
        if (GetFileAttributes(common[i]) != 0xFFFFFFFF) {
            found_count++;
            _snprintf(line, sizeof(line) - 1, "COMMON=%s\r\n", common[i]);
            line[sizeof(line) - 1] = 0;
            if (used + strlen(line) + 1 < cap) {
                strcat(out, line);
                used += strlen(line);
            }
        }
    }
    if (!found_count)
        profile_append(out, cap, "(none found)\r\n");
}

static void execute_action(const Config *cfg, const char *reply, char *result, size_t cap) {
    char action[64], path[MAX_PATH], command[BUF_SIZE];
    char full[MAX_PATH];
    field(reply, "action", action, sizeof(action));
    field(reply, "path", path, sizeof(path));
    field(reply, "command", command, sizeof(command));
    log_action(action, path[0] ? path : command);

    if (!strcmp(action, "list_dir")) {
        if (!resolve_path(cfg, path, full)) strcpy(result, "ERROR: path outside project root");
        else list_dir(full, result, cap);
    } else if (!strcmp(action, "read_file")) {
        if (!resolve_path(cfg, path, full)) strcpy(result, "ERROR: path outside project root");
        else read_file(full, result, cap);
    } else if (!strcmp(action, "write_file")) {
        char *data = (char *)malloc(BIG_SIZE);
        unsigned char *decoded = (unsigned char *)malloc(RESULT_SIZE);
        int len;
        if (!data || !decoded) {
            if (data) free(data);
            if (decoded) free(decoded);
            strcpy(result, "ERROR: not enough memory to write file");
            return;
        }
        field(reply, "data", data, BIG_SIZE);
        if (!resolve_path(cfg, path, full)) strcpy(result, "ERROR: path outside project root");
        else if (!approve(cfg, "write file", path)) strcpy(result, "DENIED by user");
        else {
            len = b64_decode(data, decoded, RESULT_SIZE);
            write_file(full, decoded, len, result);
        }
        free(decoded);
        free(data);
    } else if (!strcmp(action, "make_dir")) {
        if (!resolve_path(cfg, path, full)) strcpy(result, "ERROR: path outside project root");
        else if (!approve(cfg, "create directory", path)) strcpy(result, "DENIED by user");
        else make_dir(full, result);
    } else if (!strcmp(action, "delete_path")) {
        if (!resolve_path(cfg, path, full)) strcpy(result, "ERROR: path outside allowed access");
        else if (strlen(full) <= 3) strcpy(result, "ERROR: refusing to delete a drive root");
        else if (!approve(cfg, "delete path", full)) strcpy(result, "DENIED by user");
        else if (delete_tree(full)) strcpy(result, "OK: deleted");
        else sprintf(result, "ERROR: deletion failed (%lu)", GetLastError());
    } else if (!strcmp(action, "run_command")) {
        if (!approve(cfg, "run command", command)) strcpy(result, "DENIED by user");
        else run_command(cfg, command, result, cap);
    } else if (!strcmp(action, "run_program")) {
        if (!approve(cfg, "launch program", command)) strcpy(result, "DENIED by user");
        else run_program(cfg, command, result);
    } else if (!strcmp(action, "system_info")) {
        device_profile(cfg, result, cap);
    } else {
        sprintf(result, "ERROR: unknown action '%s'", action);
    }
}

static unsigned __stdcall task_thread(void *param) {
    Task *task = (Task *)param;
    char prompt_enc[BIG_SIZE], root_enc[BUF_SIZE], model_enc[256];
    char body[BIG_SIZE], reply[BIG_SIZE], session[128], status[64];
    char message_b64[BIG_SIZE], message[BIG_SIZE], result[RESULT_SIZE];
    char result_b64[BIG_SIZE];

    post_alloc(WM_SET_STATUS, "Working...");
    url_encode(task->prompt, prompt_enc, sizeof(prompt_enc));
    url_encode(task->cfg.root, root_enc, sizeof(root_enc));
    url_encode(task->cfg.model, model_enc, sizeof(model_enc));
    {
        char profile[BUF_SIZE], profile_b64[BUF_SIZE * 2];
        device_profile(&task->cfg, profile, sizeof(profile));
        b64_encode((unsigned char *)profile, strlen(profile), profile_b64, sizeof(profile_b64));
        _snprintf(body, sizeof(body) - 1, "prompt=%s&root=%s&profile=%s&access=%s&model=%s",
            prompt_enc, root_enc, profile_b64, task->cfg.full_access ? "full" : "project",
            model_enc);
    }
    body[sizeof(body) - 1] = 0;
    if (!http_post(&task->cfg, "/session/start", body, reply, sizeof(reply))) {
        append_connect_error(&task->cfg);
        goto done;
    }
    for (;;) {
        field(reply, "status", status, sizeof(status));
        field(reply, "session", session, sizeof(session));
        if (!strcmp(status, "message") || !strcmp(status, "error")) {
            field(reply, "message", message_b64, sizeof(message_b64));
            b64_decode(message_b64, (unsigned char *)message, sizeof(message));
            post_alloc(WM_APPEND_TEXT, !strcmp(status, "error") ? "\r\n[error] " : "\r\nCodex95: ");
            post_alloc(WM_APPEND_TEXT, message);
            post_alloc(WM_APPEND_TEXT, "\r\n");
            break;
        }
        if (strcmp(status, "action")) {
            post_alloc(WM_APPEND_TEXT, "\r\n[error] Invalid bridge response.\r\n");
            break;
        }
        execute_action(&task->cfg, reply, result, sizeof(result));
        b64_encode((unsigned char *)result, strlen(result), result_b64, sizeof(result_b64));
        _snprintf(body, sizeof(body) - 1, "session=%s&result=%s", session, result_b64);
        body[sizeof(body) - 1] = 0;
        if (!http_post(&task->cfg, "/session/continue", body, reply, sizeof(reply))) {
            post_alloc(WM_APPEND_TEXT, "\r\n[error] Connection lost while returning a result.\r\n");
            break;
        }
    }
done:
    free(task);
    PostMessage(g_main, WM_TASK_DONE, 0, 0);
    return 0;
}

static void ini_path(void) {
    char *slash;
    GetModuleFileName(NULL, g_ini, sizeof(g_ini));
    slash = strrchr(g_ini, '\\');
    if (slash) strcpy(slash + 1, "CODEX95.INI");
    else strcpy(g_ini, "CODEX95.INI");
}

static unsigned long text_hash(const char *text) {
    unsigned long hash = 2166136261UL;
    while (*text) {
        hash ^= (unsigned char)tolower((unsigned char)*text++);
        hash *= 16777619UL;
    }
    return hash;
}

static void chat_directory_for(const char *root, char *out, size_t cap) {
    char base[MAX_PATH], *slash;
    GetModuleFileName(NULL, base, sizeof(base));
    slash = strrchr(base, '\\');
    if (slash) *slash = 0;
    _snprintf(out, cap - 1, "%s\\CHATS\\%08lX", base, text_hash(root));
    out[cap - 1] = 0;
}

static void chat_path_for(const char *root, const char *model, char *out, size_t cap) {
    char directory[MAX_PATH];
    chat_directory_for(root, directory, sizeof(directory));
    ensure_directory_tree(directory);
    _snprintf(out, cap - 1, "%s\\%08lX.TXT", directory, text_hash(model));
    out[cap - 1] = 0;
}

static void save_chat(void) {
    Config cfg;
    char path[MAX_PATH];
    char *text;
    HANDLE file;
    DWORD written;
    int len;
    if (!g_transcript || !g_project) return;
    controls_to_config(&cfg);
    chat_path_for(cfg.root, cfg.model, path, sizeof(path));
    len = GetWindowTextLength(g_transcript);
    text = (char *)malloc(len + 1);
    if (!text) return;
    GetWindowText(g_transcript, text, len + 1);
    file = CreateFile(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, NULL);
    if (file != INVALID_HANDLE_VALUE) {
        WriteFile(file, text, (DWORD)len, &written, NULL);
        CloseHandle(file);
    }
    free(text);
}

static void load_chat(const char *root) {
    Config cfg;
    char path[MAX_PATH], *text;
    HANDLE file;
    DWORD size, read;
    controls_to_config(&cfg);
    chat_path_for(root, cfg.model, path, sizeof(path));
    file = CreateFile(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        SetWindowText(g_transcript, "Codex95 is ready.\r\nDescribe what you want to build.");
        return;
    }
    size = GetFileSize(file, NULL);
    if (size == 0xFFFFFFFFUL) { CloseHandle(file); return; }
    if (size > TRANSCRIPT_LIMIT) size = TRANSCRIPT_LIMIT;
    SetFilePointer(file, -(LONG)size, NULL, FILE_END);
    text = (char *)malloc(size + 1);
    if (!text) { CloseHandle(file); return; }
    if (!ReadFile(file, text, size, &read, NULL)) read = 0;
    text[read] = 0;
    CloseHandle(file);
    SetWindowText(g_transcript, text);
    free(text);
}

static void load_config(Config *cfg) {
    cfg->port = GetPrivateProfileInt("Codex95", "Port", 8787, g_ini);
    cfg->automatic = GetPrivateProfileInt("Codex95", "Automatic", 1, g_ini);
    cfg->full_access = GetPrivateProfileInt("Codex95", "FullAccess", 0, g_ini);
    cfg->dark_mode = GetPrivateProfileInt("Codex95", "DarkMode", 0, g_ini);
    cfg->text_scale = GetPrivateProfileInt("Codex95", "TextScale", 1, g_ini);
    if (cfg->text_scale < 0) cfg->text_scale = 0;
    if (cfg->text_scale > 4) cfg->text_scale = 4;
    GetPrivateProfileString("Codex95", "Host", "auto",
        cfg->host, sizeof(cfg->host), g_ini);
    GetPrivateProfileString("Codex95", "Project", "C:\\DEV\\CODEX95",
        cfg->root, sizeof(cfg->root), g_ini);
    GetPrivateProfileString("Codex95", "DeviceName", "Toshiba Libretto 70CT",
        cfg->device_name, sizeof(cfg->device_name), g_ini);
    GetPrivateProfileString("Codex95", "Model", "gpt-5.4-mini",
        cfg->model, sizeof(cfg->model), g_ini);
}

static void save_config(const Config *cfg) {
    char number[32];
    WritePrivateProfileString("Codex95", "Host", cfg->host, g_ini);
    WritePrivateProfileString("Codex95", "Project", cfg->root, g_ini);
    WritePrivateProfileString("Codex95", "DeviceName", cfg->device_name, g_ini);
    WritePrivateProfileString("Codex95", "Model", cfg->model, g_ini);
    sprintf(number, "%d", cfg->port);
    WritePrivateProfileString("Codex95", "Port", number, g_ini);
    WritePrivateProfileString("Codex95", "Automatic", cfg->automatic ? "1" : "0", g_ini);
    WritePrivateProfileString("Codex95", "FullAccess", cfg->full_access ? "1" : "0", g_ini);
    WritePrivateProfileString("Codex95", "DarkMode", cfg->dark_mode ? "1" : "0", g_ini);
    sprintf(number, "%d", cfg->text_scale);
    WritePrivateProfileString("Codex95", "TextScale", number, g_ini);
    WritePrivateProfileString(NULL, NULL, NULL, g_ini);
}

static void controls_to_config(Config *cfg) {
    char host_port[160], *colon;
    GetPrivateProfileString("Codex95", "DeviceName", "Toshiba Libretto 70CT",
        cfg->device_name, sizeof(cfg->device_name), g_ini);
    GetPrivateProfileString("Codex95", "Model", "gpt-5.4-mini",
        cfg->model, sizeof(cfg->model), g_ini);
    GetWindowText(g_project, cfg->root, sizeof(cfg->root));
    GetWindowText(g_host, host_port, sizeof(host_port));
    colon = strrchr(host_port, ':');
    cfg->port = 8787;
    if (colon) {
        *colon = 0;
        cfg->port = atoi(colon + 1);
    }
    strncpy(cfg->host, host_port, sizeof(cfg->host) - 1);
    cfg->host[sizeof(cfg->host) - 1] = 0;
    cfg->automatic = SendMessage(g_auto, BM_GETCHECK, 0, 0) == BST_CHECKED;
    cfg->full_access = GetPrivateProfileInt("Codex95", "FullAccess", 0, g_ini);
    cfg->dark_mode = GetPrivateProfileInt("Codex95", "DarkMode", 0, g_ini);
    cfg->text_scale = GetPrivateProfileInt("Codex95", "TextScale", 1, g_ini);
    if (cfg->text_scale < 0) cfg->text_scale = 0;
    if (cfg->text_scale > 4) cfg->text_scale = 4;
}

static void ensure_directory_tree(const char *path) {
    char copy[MAX_PATH];
    char *p;
    strncpy(copy, path, sizeof(copy) - 1);
    copy[sizeof(copy) - 1] = 0;
    p = copy;
    if (p[0] && p[1] == ':') p += 3;
    while (*p) {
        if (*p == '\\' || *p == '/') {
            char saved = *p;
            *p = 0;
            CreateDirectory(copy, NULL);
            *p = saved;
        }
        p++;
    }
    CreateDirectory(copy, NULL);
}

static void parent_directory(const char *path, char *parent, size_t cap) {
    char *slash;
    strncpy(parent, path, cap - 1);
    parent[cap - 1] = 0;
    while (strlen(parent) > 3 &&
        (parent[strlen(parent) - 1] == '\\' || parent[strlen(parent) - 1] == '/'))
        parent[strlen(parent) - 1] = 0;
    slash = strrchr(parent, '\\');
    if (!slash) slash = strrchr(parent, '/');
    if (slash && slash > parent + 2) *slash = 0;
    else if (slash) slash[1] = 0;
}

static const char *base_name(const char *path) {
    const char *a = strrchr(path, '\\');
    const char *b = strrchr(path, '/');
    const char *last = a > b ? a : b;
    return last ? last + 1 : path;
}

static void set_project_status(const char *path) {
    char text[MAX_PATH + 128], model[80];
    GetPrivateProfileString("Codex95", "Model", "gpt-5.4-mini",
        model, sizeof(model), g_ini);
    if (GetPrivateProfileInt("Codex95", "FullAccess", 0, g_ini)) {
        _snprintf(text, sizeof(text) - 1, "%s  [%s]  [FULL ACCESS]", base_name(path), model);
        text[sizeof(text) - 1] = 0;
        SetWindowText(g_status, text);
    } else {
        _snprintf(text, sizeof(text) - 1, "%s  [%s]", base_name(path), model);
        text[sizeof(text) - 1] = 0;
        SetWindowText(g_status, text);
    }
}

static void refresh_projects(void) {
    WIN32_FIND_DATA fd;
    HANDLE find;
    char active[MAX_PATH], parent[MAX_PATH], pattern[MAX_PATH];
    int select_index = -1, index = 0;
    GetWindowText(g_project, active, sizeof(active));
    parent_directory(active, parent, sizeof(parent));
    SendMessage(g_projects, LB_RESETCONTENT, 0, 0);
    if (_snprintf(pattern, sizeof(pattern) - 1, "%s\\*.*", parent) < 0) return;
    pattern[sizeof(pattern) - 1] = 0;
    find = FindFirstFile(pattern, &fd);
    if (find == INVALID_HANDLE_VALUE) return;
    do {
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
            strcmp(fd.cFileName, ".") && strcmp(fd.cFileName, "..")) {
            SendMessage(g_projects, LB_ADDSTRING, 0, (LPARAM)fd.cFileName);
            if (!_stricmp(fd.cFileName, base_name(active))) select_index = index;
            index++;
        }
    } while (FindNextFile(find, &fd));
    FindClose(find);
    if (select_index >= 0) SendMessage(g_projects, LB_SETCURSEL, select_index, 0);
}

static void switch_project(void) {
    int index;
    char active[MAX_PATH], parent[MAX_PATH], name[MAX_PATH], full[MAX_PATH];
    if (g_busy) return;
    index = (int)SendMessage(g_projects, LB_GETCURSEL, 0, 0);
    if (index == LB_ERR) return;
    SendMessage(g_projects, LB_GETTEXT, index, (LPARAM)name);
    GetWindowText(g_project, active, sizeof(active));
    parent_directory(active, parent, sizeof(parent));
    _snprintf(full, sizeof(full) - 1, "%s\\%s", parent, name);
    full[sizeof(full) - 1] = 0;
    save_chat();
    SetWindowText(g_project, full);
    WritePrivateProfileString("Codex95", "Project", full, g_ini);
    WritePrivateProfileString(NULL, NULL, NULL, g_ini);
    load_chat(full);
    set_project_status(full);
}

static int valid_project_name(const char *name) {
    const char *invalid = "\\/:*?\"<>|";
    return name[0] && strcmp(name, ".") && strcmp(name, "..") &&
        strpbrk(name, invalid) == NULL;
}

static LRESULT CALLBACK name_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        HWND edit = CreateWindowEx(WS_EX_CLIENTEDGE, "EDIT", g_name_value,
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 12, 14, 300, 22,
            hwnd, (HMENU)IDC_NAME_EDIT, NULL, NULL);
        HWND ok = CreateWindow("BUTTON", "OK", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            152, 48, 76, 24, hwnd, (HMENU)IDC_NAME_OK, NULL, NULL);
        HWND cancel = CreateWindow("BUTTON", "Cancel", WS_CHILD | WS_VISIBLE,
            236, 48, 76, 24, hwnd, (HMENU)IDC_NAME_CANCEL, NULL, NULL);
        SendMessage(edit, WM_SETFONT, (WPARAM)font, TRUE);
        SendMessage(ok, WM_SETFONT, (WPARAM)font, TRUE);
        SendMessage(cancel, WM_SETFONT, (WPARAM)font, TRUE);
        SendMessage(edit, EM_LIMITTEXT, 120, 0);
        SetFocus(edit);
        SendMessage(edit, EM_SETSEL, 0, -1);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == IDC_NAME_OK) {
            GetWindowText(GetDlgItem(hwnd, IDC_NAME_EDIT), g_name_value, sizeof(g_name_value));
            if (!valid_project_name(g_name_value)) {
                MessageBox(hwnd, "Enter a valid folder name without \\ / : * ? \" < > |",
                    APP_TITLE, MB_OK | MB_ICONEXCLAMATION);
                return 0;
            }
            g_name_done = 1;
            DestroyWindow(hwnd);
        } else if (LOWORD(wp) == IDC_NAME_CANCEL) DestroyWindow(hwnd);
        return 0;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

static int ask_project_name(const char *title, const char *initial, char *out, size_t cap) {
    HWND window;
    MSG msg;
    RECT rc;
    strncpy(g_name_value, initial, sizeof(g_name_value) - 1);
    g_name_value[sizeof(g_name_value) - 1] = 0;
    g_name_done = 0;
    GetWindowRect(g_main, &rc);
    window = CreateWindowEx(WS_EX_DLGMODALFRAME, "Codex95Name", title,
        WS_POPUP | WS_CAPTION | WS_SYSMENU, rc.left + 120, rc.top + 100, 340, 112,
        g_main, NULL, NULL, NULL);
    if (!window) return 0;
    EnableWindow(g_main, FALSE);
    ShowWindow(window, SW_SHOW);
    while (IsWindow(window) && GetMessage(&msg, NULL, 0, 0)) {
        if (!IsDialogMessage(window, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
    EnableWindow(g_main, TRUE);
    SetActiveWindow(g_main);
    if (!g_name_done) return 0;
    strncpy(out, g_name_value, cap - 1);
    out[cap - 1] = 0;
    return 1;
}

static void new_project(void) {
    char active[MAX_PATH], parent[MAX_PATH], full[MAX_PATH];
    char name[128];
    int number;
    if (g_busy) return;
    GetWindowText(g_project, active, sizeof(active));
    parent_directory(active, parent, sizeof(parent));
    ensure_directory_tree(parent);
    for (number = 1; number < 1000; number++) {
        _snprintf(full, sizeof(full) - 1, "%s\\PROJECT%02d", parent, number);
        full[sizeof(full) - 1] = 0;
        if (GetFileAttributes(full) == 0xFFFFFFFF) break;
    }
    strncpy(name, base_name(full), sizeof(name) - 1);
    name[sizeof(name) - 1] = 0;
    if (!ask_project_name("New project name", name, name, sizeof(name))) return;
    _snprintf(full, sizeof(full) - 1, "%s\\%s", parent, name);
    full[sizeof(full) - 1] = 0;
    if (GetFileAttributes(full) != 0xFFFFFFFF) {
        MessageBox(g_main, "A project with that name already exists.", APP_TITLE,
            MB_OK | MB_ICONEXCLAMATION);
        return;
    }
    save_chat();
    ensure_directory_tree(full);
    SetWindowText(g_project, full);
    WritePrivateProfileString("Codex95", "Project", full, g_ini);
    WritePrivateProfileString(NULL, NULL, NULL, g_ini);
    refresh_projects();
    load_chat(full);
    set_project_status(full);
    SetFocus(g_prompt);
}

static void rename_project(void) {
    int index;
    int active_renamed;
    char active[MAX_PATH], parent[MAX_PATH], old_name[MAX_PATH], new_name[128];
    char old_path[MAX_PATH], new_path[MAX_PATH], old_chat[MAX_PATH], new_chat[MAX_PATH];
    if (g_busy) return;
    index = (int)SendMessage(g_projects, LB_GETCURSEL, 0, 0);
    if (index == LB_ERR) { MessageBox(g_main, "Select a project to rename.", APP_TITLE, MB_OK); return; }
    SendMessage(g_projects, LB_GETTEXT, index, (LPARAM)old_name);
    if (!ask_project_name("Rename project", old_name, new_name, sizeof(new_name))) return;
    GetWindowText(g_project, active, sizeof(active));
    parent_directory(active, parent, sizeof(parent));
    _snprintf(old_path, sizeof(old_path) - 1, "%s\\%s", parent, old_name);
    _snprintf(new_path, sizeof(new_path) - 1, "%s\\%s", parent, new_name);
    old_path[sizeof(old_path) - 1] = new_path[sizeof(new_path) - 1] = 0;
    active_renamed = !_stricmp(active, old_path);
    if (GetFileAttributes(new_path) != 0xFFFFFFFF) {
        MessageBox(g_main, "A project with that name already exists.", APP_TITLE, MB_OK | MB_ICONEXCLAMATION);
        return;
    }
    save_chat();
    if (!MoveFile(old_path, new_path)) {
        MessageBox(g_main, "Could not rename project. Close programs opened from it and try again.",
            APP_TITLE, MB_OK | MB_ICONSTOP);
        return;
    }
    chat_directory_for(old_path, old_chat, sizeof(old_chat));
    chat_directory_for(new_path, new_chat, sizeof(new_chat));
    MoveFile(old_chat, new_chat);
    if (active_renamed) {
        SetWindowText(g_project, new_path);
        WritePrivateProfileString("Codex95", "Project", new_path, g_ini);
        WritePrivateProfileString(NULL, NULL, NULL, g_ini);
        strcpy(active, new_path);
    }
    refresh_projects();
    if (active_renamed) load_chat(new_path);
    set_project_status(active);
}

static void project_after_delete(const char *parent, const char *deleted_name,
                                 char *out, size_t cap) {
    WIN32_FIND_DATA fd;
    HANDLE find;
    char pattern[MAX_PATH];
    int number;
    out[0] = 0;
    _snprintf(pattern, sizeof(pattern) - 1, "%s\\*.*", parent);
    pattern[sizeof(pattern) - 1] = 0;
    find = FindFirstFile(pattern, &fd);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                strcmp(fd.cFileName, ".") && strcmp(fd.cFileName, "..") &&
                _stricmp(fd.cFileName, deleted_name)) {
                _snprintf(out, cap - 1, "%s\\%s", parent, fd.cFileName);
                out[cap - 1] = 0;
                break;
            }
        } while (FindNextFile(find, &fd));
        FindClose(find);
    }
    if (out[0]) return;
    for (number = 1; number < 1000; number++) {
        char name[32];
        sprintf(name, "PROJECT%02d", number);
        if (!_stricmp(name, deleted_name)) continue;
        _snprintf(out, cap - 1, "%s\\%s", parent, name);
        out[cap - 1] = 0;
        if (GetFileAttributes(out) == 0xFFFFFFFF) break;
    }
    ensure_directory_tree(out);
}

static void delete_project(void) {
    int index;
    int active_deleted;
    char active[MAX_PATH], parent[MAX_PATH], name[MAX_PATH], full[MAX_PATH], text[BUF_SIZE];
    char chat_path[MAX_PATH];
    if (g_busy) return;
    index = (int)SendMessage(g_projects, LB_GETCURSEL, 0, 0);
    if (index == LB_ERR) {
        MessageBox(g_main, "Select a project to delete.", APP_TITLE, MB_OK | MB_ICONINFORMATION);
        return;
    }
    SendMessage(g_projects, LB_GETTEXT, index, (LPARAM)name);
    GetWindowText(g_project, active, sizeof(active));
    parent_directory(active, parent, sizeof(parent));
    _snprintf(full, sizeof(full) - 1, "%s\\%s", parent, name);
    full[sizeof(full) - 1] = 0;
    if (strlen(full) <= 3 || !starts_with_ci(full, parent)) {
        MessageBox(g_main, "Codex95 refused to delete this path.", APP_TITLE, MB_OK | MB_ICONSTOP);
        return;
    }
    _snprintf(text, sizeof(text) - 1,
        "Delete project '%s' and ALL files inside it?\r\n\r\n%s\r\n\r\nThis cannot be undone.",
        name, full);
    text[sizeof(text) - 1] = 0;
    if (MessageBox(g_main, text, APP_TITLE, MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
        return;
    if (MessageBox(g_main, "Final confirmation: permanently delete this project?",
        APP_TITLE, MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
        return;
    if (!delete_tree(full)) {
        _snprintf(text, sizeof(text) - 1,
            "Could not completely delete the project.\r\nError: %lu\r\n\r\n"
            "Close programs and files opened from this project, then try again.",
            GetLastError());
        text[sizeof(text) - 1] = 0;
        MessageBox(g_main, text, APP_TITLE, MB_OK | MB_ICONSTOP);
        return;
    }
    chat_directory_for(full, chat_path, sizeof(chat_path));
    delete_tree(chat_path);
    active_deleted = !_stricmp(active, full);
    if (active_deleted) {
        project_after_delete(parent, name, active, sizeof(active));
        SetWindowText(g_project, active);
        WritePrivateProfileString("Codex95", "Project", active, g_ini);
        WritePrivateProfileString(NULL, NULL, NULL, g_ini);
    }
    refresh_projects();
    if (active_deleted) load_chat(active);
    set_project_status(active);
}

static void browse_project(void) {
    BROWSEINFO bi;
    LPITEMIDLIST id;
    IMalloc *allocator;
    char path[MAX_PATH];
    memset(&bi, 0, sizeof(bi));
    bi.hwndOwner = g_main;
    bi.lpszTitle = "Select the project folder";
    bi.ulFlags = BIF_RETURNONLYFSDIRS;
    id = SHBrowseForFolder(&bi);
    if (id && SHGetPathFromIDList(id, path)) {
        save_chat();
        SetWindowText(g_project, path);
        WritePrivateProfileString("Codex95", "Project", path, g_ini);
        WritePrivateProfileString(NULL, NULL, NULL, g_ini);
        refresh_projects();
        load_chat(path);
        set_project_status(path);
    }
    if (id && SUCCEEDED(SHGetMalloc(&allocator))) {
        allocator->lpVtbl->Free(allocator, id);
        allocator->lpVtbl->Release(allocator);
    }
}

static LRESULT CALLBACK settings_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        HFONT font = g_ui_font ? g_ui_font : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        char host_port[160];
        HWND child;
        sprintf(host_port, "%s:%d", g_settings_cfg.host, g_settings_cfg.port);
        CreateWindow("STATIC", "Bridge address:", WS_CHILD | WS_VISIBLE, 12, 15, 105, 18,
            hwnd, NULL, NULL, NULL);
        child = CreateWindowEx(WS_EX_CLIENTEDGE, "EDIT", host_port,
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 120, 12, 230, 22,
            hwnd, (HMENU)IDC_SET_HOST, NULL, NULL);
        SendMessage(child, WM_SETFONT, (WPARAM)font, TRUE);
        CreateWindow("STATIC", "Device name:", WS_CHILD | WS_VISIBLE, 12, 47, 105, 18,
            hwnd, NULL, NULL, NULL);
        child = CreateWindowEx(WS_EX_CLIENTEDGE, "EDIT", g_settings_cfg.device_name,
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 120, 44, 230, 22,
            hwnd, (HMENU)IDC_SET_DEVICE, NULL, NULL);
        SendMessage(child, WM_SETFONT, (WPARAM)font, TRUE);
        CreateWindow("STATIC", "Model:", WS_CHILD | WS_VISIBLE, 12, 79, 105, 18,
            hwnd, NULL, NULL, NULL);
        child = CreateWindowEx(WS_EX_CLIENTEDGE, "COMBOBOX", "",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWN,
            120, 76, 230, 120, hwnd, (HMENU)IDC_SET_MODEL, NULL, NULL);
        SendMessage(child, WM_SETFONT, (WPARAM)font, TRUE);
        SendMessage(child, CB_ADDSTRING, 0, (LPARAM)"gpt-5.4-nano");
        SendMessage(child, CB_ADDSTRING, 0, (LPARAM)"gpt-5.4-mini");
        SendMessage(child, CB_ADDSTRING, 0, (LPARAM)"gpt-5.4");
        SetWindowText(child, g_settings_cfg.model);
        child = CreateWindow("BUTTON", "Run actions automatically",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 12, 112, 250, 20,
            hwnd, (HMENU)IDC_SET_AUTO, NULL, NULL);
        SendMessage(child, WM_SETFONT, (WPARAM)font, TRUE);
        SendMessage(child, BM_SETCHECK, g_settings_cfg.automatic ? BST_CHECKED : BST_UNCHECKED, 0);
        child = CreateWindow("BUTTON", "Full computer access",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 12, 138, 250, 20,
            hwnd, (HMENU)IDC_SET_FULL, NULL, NULL);
        SendMessage(child, WM_SETFONT, (WPARAM)font, TRUE);
        SendMessage(child, BM_SETCHECK, g_settings_cfg.full_access ? BST_CHECKED : BST_UNCHECKED, 0);
        child = CreateWindow("BUTTON", "Dark interface",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 12, 164, 250, 20,
            hwnd, (HMENU)IDC_SET_DARK, NULL, NULL);
        SendMessage(child, WM_SETFONT, (WPARAM)font, TRUE);
        SendMessage(child, BM_SETCHECK, g_settings_cfg.dark_mode ? BST_CHECKED : BST_UNCHECKED, 0);
        CreateWindow("STATIC", "Text size:", WS_CHILD | WS_VISIBLE, 12, 192, 105, 18,
            hwnd, NULL, NULL, NULL);
        child = CreateWindow("SCROLLBAR", "",
            WS_CHILD | WS_VISIBLE | SBS_HORZ, 120, 190, 230, 18,
            hwnd, (HMENU)IDC_SET_SCALE, NULL, NULL);
        SendMessage(child, SBM_SETRANGE, 0, 4);
        SendMessage(child, SBM_SETPOS, g_settings_cfg.text_scale, TRUE);
        child = CreateWindow("STATIC",
            "Full access allows Codex to use absolute paths, run commands anywhere,\r\n"
            "and modify or delete files outside the selected project.",
            WS_CHILD | WS_VISIBLE, 30, 216, 320, 42, hwnd, NULL, NULL, NULL);
        SendMessage(child, WM_SETFONT, (WPARAM)font, TRUE);
        child = CreateWindow("BUTTON", "OK", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            190, 268, 76, 24, hwnd, (HMENU)IDC_SET_OK, NULL, NULL);
        SendMessage(child, WM_SETFONT, (WPARAM)font, TRUE);
        child = CreateWindow("BUTTON", "Cancel", WS_CHILD | WS_VISIBLE,
            274, 268, 76, 24, hwnd, (HMENU)IDC_SET_CANCEL, NULL, NULL);
        SendMessage(child, WM_SETFONT, (WPARAM)font, TRUE);
        return 0;
    }
    case WM_HSCROLL:
        if ((HWND)lp == GetDlgItem(hwnd, IDC_SET_SCALE)) {
            int pos = GetScrollPos((HWND)lp, SB_CTL);
            switch (LOWORD(wp)) {
            case SB_LINELEFT: if (pos > 0) pos--; break;
            case SB_LINERIGHT: if (pos < 4) pos++; break;
            case SB_PAGELEFT: pos -= 1; if (pos < 0) pos = 0; break;
            case SB_PAGERIGHT: pos += 1; if (pos > 4) pos = 4; break;
            case SB_THUMBPOSITION:
            case SB_THUMBTRACK: pos = HIWORD(wp); break;
            }
            if (pos < 0) pos = 0;
            if (pos > 4) pos = 4;
            SetScrollPos((HWND)lp, SB_CTL, pos, TRUE);
            return 0;
        }
        break;
    case WM_COMMAND:
        if (LOWORD(wp) == IDC_SET_OK) {
            char host_port[160], *colon;
            int full = SendMessage(GetDlgItem(hwnd, IDC_SET_FULL), BM_GETCHECK, 0, 0) == BST_CHECKED;
            if (full && !g_settings_cfg.full_access &&
                MessageBox(hwnd,
                    "Full computer access lets Codex modify and delete files anywhere on this computer.\r\n\r\nEnable it?",
                    APP_TITLE, MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
                return 0;
            GetWindowText(GetDlgItem(hwnd, IDC_SET_HOST), host_port, sizeof(host_port));
            colon = strrchr(host_port, ':');
            g_settings_cfg.port = 8787;
            if (colon) { *colon = 0; g_settings_cfg.port = atoi(colon + 1); }
            strncpy(g_settings_cfg.host, host_port, sizeof(g_settings_cfg.host) - 1);
            g_settings_cfg.host[sizeof(g_settings_cfg.host) - 1] = 0;
            GetWindowText(GetDlgItem(hwnd, IDC_SET_DEVICE), g_settings_cfg.device_name,
                sizeof(g_settings_cfg.device_name));
            GetWindowText(GetDlgItem(hwnd, IDC_SET_MODEL), g_settings_cfg.model,
                sizeof(g_settings_cfg.model));
            if (!g_settings_cfg.model[0]) strcpy(g_settings_cfg.model, "gpt-5.4-mini");
            g_settings_cfg.automatic =
                SendMessage(GetDlgItem(hwnd, IDC_SET_AUTO), BM_GETCHECK, 0, 0) == BST_CHECKED;
            g_settings_cfg.full_access = full;
            g_settings_cfg.dark_mode =
                SendMessage(GetDlgItem(hwnd, IDC_SET_DARK), BM_GETCHECK, 0, 0) == BST_CHECKED;
            g_settings_cfg.text_scale = GetScrollPos(GetDlgItem(hwnd, IDC_SET_SCALE), SB_CTL);
            g_dark_mode = g_settings_cfg.dark_mode;
            g_text_scale = g_settings_cfg.text_scale;
            save_config(&g_settings_cfg);
            sprintf(host_port, "%s:%d", g_settings_cfg.host, g_settings_cfg.port);
            SetWindowText(g_host, host_port);
            SendMessage(g_auto, BM_SETCHECK,
                g_settings_cfg.automatic ? BST_CHECKED : BST_UNCHECKED, 0);
            apply_theme();
            g_settings_done = 1;
            DestroyWindow(hwnd);
        } else if (LOWORD(wp) == IDC_SET_CANCEL) {
            DestroyWindow(hwnd);
        }
        return 0;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
        {
            HBRUSH brush = control_color((HDC)wp, 1);
            if (brush) return (LRESULT)brush;
        }
        break;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
        {
            HBRUSH brush = control_color((HDC)wp, 0);
            if (brush) return (LRESULT)brush;
        }
        break;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

static void show_settings(void) {
    HWND window;
    MSG msg;
    RECT main_rc;
    if (g_busy) return;
    save_chat();
    load_config(&g_settings_cfg);
    controls_to_config(&g_settings_cfg);
    g_settings_done = 0;
    GetWindowRect(g_main, &main_rc);
    window = CreateWindowEx(WS_EX_DLGMODALFRAME, "Codex95Settings", "Codex95 Settings",
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        main_rc.left + 80, main_rc.top + 60, 380, 336,
        g_main, NULL, NULL, NULL);
    if (!window) return;
    EnableWindow(g_main, FALSE);
    ShowWindow(window, SW_SHOW);
    while (IsWindow(window) && GetMessage(&msg, NULL, 0, 0)) {
        if (!IsDialogMessage(window, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
    EnableWindow(g_main, TRUE);
    SetActiveWindow(g_main);
    if (g_settings_done) {
        load_chat(g_settings_cfg.root);
        set_project_status(g_settings_cfg.root);
    }
}

static int write_update_batch(const char *batch_path) {
    HANDLE file;
    DWORD written;
    const char *text =
        "@echo off\r\n"
        "echo Updating Codex95...\r\n"
        ":again\r\n"
        "del CODEX95W.EXE >NUL\r\n"
        "if exist CODEX95W.EXE goto again\r\n"
        "copy CODEX95W.NEW CODEX95W.EXE >NUL\r\n"
        "if not exist CODEX95W.EXE goto again\r\n"
        "del CODEX95W.NEW >NUL\r\n"
        "CODEX95W.EXE\r\n";
    file = CreateFile(batch_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return 0;
    WriteFile(file, text, (DWORD)strlen(text), &written, NULL);
    CloseHandle(file);
    return 1;
}

static void update_client(void) {
    Config cfg;
    char base[MAX_PATH], *slash, new_path[MAX_PATH], batch_path[MAX_PATH];
    char cmd[MAX_PATH + 64], host_port[160];
    STARTUPINFO si;
    PROCESS_INFORMATION pi;
    if (g_busy) return;
    controls_to_config(&cfg);
    if (!strcmp(cfg.host, "auto")) {
        SetWindowText(g_status, "Finding bridge...");
        if (!discover_bridge(&cfg)) {
            MessageBox(g_main, "Could not find the bridge. Enter its address manually first.",
                APP_TITLE, MB_OK | MB_ICONEXCLAMATION);
            SetWindowText(g_status, "Ready");
            return;
        }
        sprintf(host_port, "%s:%d", cfg.host, cfg.port);
        SetWindowText(g_host, host_port);
        save_config(&cfg);
    }
    if (MessageBox(g_main,
        "Download the latest Codex95 client from the bridge and restart?\r\n\r\n"
        "The updater will replace CODEX95W.EXE after this window closes.",
        APP_TITLE, MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    GetModuleFileName(NULL, base, sizeof(base));
    slash = strrchr(base, '\\');
    if (!slash) return;
    strcpy(slash + 1, "CODEX95W.NEW");
    strcpy(new_path, base);
    strcpy(slash + 1, "APPLYUPD.BAT");
    strcpy(batch_path, base);
    strcpy(slash + 1, "");
    SetWindowText(g_status, "Downloading update...");
    if (!http_get_file(&cfg, "/client/CODEX95W.EXE", new_path)) {
        MessageBox(g_main, "Could not download update from the bridge.",
            APP_TITLE, MB_OK | MB_ICONSTOP);
        SetWindowText(g_status, "Ready");
        return;
    }
    if (!write_update_batch(batch_path)) {
        MessageBox(g_main, "Could not create APPLYUPD.BAT.", APP_TITLE, MB_OK | MB_ICONSTOP);
        SetWindowText(g_status, "Ready");
        return;
    }
    memset(&si, 0, sizeof(si));
    memset(&pi, 0, sizeof(pi));
    si.cb = sizeof(si);
    strcpy(cmd, "COMMAND.COM /C APPLYUPD.BAT");
    if (!CreateProcess(NULL, cmd, NULL, NULL, FALSE, 0, NULL, base, &si, &pi)) {
        MessageBox(g_main, "Could not start updater batch file.", APP_TITLE, MB_OK | MB_ICONSTOP);
        SetWindowText(g_status, "Ready");
        return;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    PostMessage(g_main, WM_CLOSE, 0, 0);
}

static void start_task(void) {
    Task *task;
    HANDLE thread;
    unsigned id;
    int len;
    if (g_busy) return;
    len = GetWindowTextLength(g_prompt);
    if (!len) return;
    task = (Task *)calloc(1, sizeof(Task));
    if (!task) return;
    GetWindowText(g_prompt, task->prompt, sizeof(task->prompt));
    controls_to_config(&task->cfg);
    if (!task->cfg.root[0] || !task->cfg.host[0]) {
        MessageBox(g_main, "Choose a project folder and enter the bridge address.",
            APP_TITLE, MB_OK | MB_ICONEXCLAMATION);
        free(task);
        return;
    }
    if (!strcmp(task->cfg.host, "auto")) {
        char host_port[160];
        SetWindowText(g_status, "Finding bridge...");
        if (!discover_bridge(&task->cfg)) {
            MessageBox(g_main,
                "Codex95 could not find the bridge automatically.\r\n"
                "Start the bridge or enter its address manually.",
                APP_TITLE, MB_OK | MB_ICONEXCLAMATION);
            SetWindowText(g_status, "Ready");
            free(task);
            return;
        }
        sprintf(host_port, "%s:%d", task->cfg.host, task->cfg.port);
        SetWindowText(g_host, host_port);
    }
    ensure_directory_tree(task->cfg.root);
    save_config(&task->cfg);
    post_alloc(WM_APPEND_TEXT, "\r\nYou: ");
    post_alloc(WM_APPEND_TEXT, task->prompt);
    post_alloc(WM_APPEND_TEXT, "\r\n");
    SetWindowText(g_prompt, "");
    g_busy = 1;
    EnableWindow(g_send, FALSE);
    EnableWindow(g_projects, FALSE);
    EnableWindow(GetDlgItem(g_main, IDC_NEW_PROJECT), FALSE);
    EnableWindow(GetDlgItem(g_main, IDC_DELETE_PROJECT), FALSE);
    EnableWindow(GetDlgItem(g_main, IDC_RENAME_PROJECT), FALSE);
    thread = (HANDLE)_beginthreadex(NULL, 768 * 1024, task_thread, task, 0, &id);
    if (thread) CloseHandle(thread);
    else {
        g_busy = 0;
        EnableWindow(g_send, TRUE);
        EnableWindow(g_projects, TRUE);
        EnableWindow(GetDlgItem(g_main, IDC_NEW_PROJECT), TRUE);
        EnableWindow(GetDlgItem(g_main, IDC_DELETE_PROJECT), TRUE);
        EnableWindow(GetDlgItem(g_main, IDC_RENAME_PROJECT), TRUE);
        free(task);
    }
}

static void resize_controls(HWND hwnd) {
    RECT rc;
    int w, h, top = 42, bottom = 82, side = 128, content = 136;
    GetClientRect(hwnd, &rc);
    w = rc.right;
    h = rc.bottom;
    MoveWindow(g_project, 60, 8, w - 144, 22, TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_BROWSE), w - 76, 8, 68, 22, TRUE);
    MoveWindow(g_projects, 8, top + 18, side - 16, h - top - bottom - 102, TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_NEW_PROJECT), 8, h - bottom - 78, 54, 22, TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_REFRESH_PROJECTS), 66, h - bottom - 78, 54, 22, TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_RENAME_PROJECT), 8, h - bottom - 52, 112, 22, TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_DELETE_PROJECT), 8, h - bottom - 26, 112, 22, TRUE);
    MoveWindow(g_transcript, content, top, w - content - 8, h - top - bottom, TRUE);
    MoveWindow(g_prompt, content, h - bottom + 8, w - content - 96, 54, TRUE);
    MoveWindow(g_send, w - 88, h - bottom + 8, 80, 54, TRUE);
    MoveWindow(g_status, content, h - 20, w - content - 8, 18, TRUE);
}

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        Config cfg;
        char host_port[160];
        HFONT font = g_ui_font ? g_ui_font : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        g_host = CreateWindow("EDIT", "", WS_CHILD,
            0, 0, 1, 1, hwnd, (HMENU)IDC_HOST, NULL, NULL);
        CreateWindow("STATIC", "Project:", WS_CHILD | WS_VISIBLE, 8, 11, 50, 18,
            hwnd, NULL, NULL, NULL);
        g_project = CreateWindowEx(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            60, 8, 518, 22, hwnd, (HMENU)IDC_PROJECT, NULL, NULL);
        CreateWindow("BUTTON", "Browse...", WS_CHILD | WS_VISIBLE,
            584, 8, 68, 22, hwnd, (HMENU)IDC_BROWSE, NULL, NULL);
        CreateWindow("STATIC", "Projects", WS_CHILD | WS_VISIBLE,
            8, 44, 112, 16, hwnd, NULL, NULL, NULL);
        g_projects = CreateWindowEx(WS_EX_CLIENTEDGE, "LISTBOX", "",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
            8, 60, 112, 250, hwnd, (HMENU)IDC_PROJECTS, NULL, NULL);
        CreateWindow("BUTTON", "New", WS_CHILD | WS_VISIBLE,
            8, 314, 54, 22, hwnd, (HMENU)IDC_NEW_PROJECT, NULL, NULL);
        CreateWindow("BUTTON", "Refresh", WS_CHILD | WS_VISIBLE,
            66, 314, 54, 22, hwnd, (HMENU)IDC_REFRESH_PROJECTS, NULL, NULL);
        CreateWindow("BUTTON", "Delete project", WS_CHILD | WS_VISIBLE,
            8, 340, 112, 22, hwnd, (HMENU)IDC_DELETE_PROJECT, NULL, NULL);
        CreateWindow("BUTTON", "Rename project", WS_CHILD | WS_VISIBLE,
            8, 366, 112, 22, hwnd, (HMENU)IDC_RENAME_PROJECT, NULL, NULL);
        g_transcript = CreateWindowEx(WS_EX_CLIENTEDGE, "EDIT",
            "Codex95 is ready.\r\nDescribe what you want to build.",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL |
            ES_READONLY, 8, 42, 640, 300, hwnd, (HMENU)IDC_TRANSCRIPT, NULL, NULL);
        g_prompt = CreateWindowEx(WS_EX_CLIENTEDGE, "EDIT", "",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL,
            8, 350, 540, 54, hwnd, (HMENU)IDC_PROMPT, NULL, NULL);
        g_send = CreateWindow("BUTTON", "Build it", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            556, 350, 92, 54, hwnd, (HMENU)IDC_SEND, NULL, NULL);
        g_auto = CreateWindow("BUTTON", "", WS_CHILD | BS_AUTOCHECKBOX,
            0, 0, 1, 1, hwnd, (HMENU)IDC_AUTO, NULL, NULL);
        g_status = CreateWindow("STATIC", "Ready", WS_CHILD | WS_VISIBLE,
            136, 412, 512, 18, hwnd, (HMENU)IDC_STATUS, NULL, NULL);
        SendMessage(g_transcript, EM_LIMITTEXT, TRANSCRIPT_LIMIT + 2048, 0);
        SendMessage(g_prompt, EM_LIMITTEXT, BUF_SIZE - 1, 0);
        SendMessage(g_project, EM_LIMITTEXT, MAX_PATH - 1, 0);
        SendMessage(g_host, EM_LIMITTEXT, 150, 0);
        SendMessage(g_transcript, WM_SETFONT, (WPARAM)font, TRUE);
        SendMessage(g_prompt, WM_SETFONT, (WPARAM)font, TRUE);
        SendMessage(g_project, WM_SETFONT, (WPARAM)font, TRUE);
        SendMessage(g_host, WM_SETFONT, (WPARAM)font, TRUE);
        SendMessage(g_send, WM_SETFONT, (WPARAM)font, TRUE);
        SendMessage(g_auto, WM_SETFONT, (WPARAM)font, TRUE);
        SendMessage(g_status, WM_SETFONT, (WPARAM)font, TRUE);
        SendMessage(g_projects, WM_SETFONT, (WPARAM)font, TRUE);
        load_config(&cfg);
        sprintf(host_port, "%s:%d", cfg.host, cfg.port);
        SetWindowText(g_host, host_port);
        SetWindowText(g_project, cfg.root);
        SendMessage(g_auto, BM_SETCHECK, cfg.automatic ? BST_CHECKED : BST_UNCHECKED, 0);
        ensure_directory_tree(cfg.root);
        refresh_projects();
        load_chat(cfg.root);
        set_project_status(cfg.root);
        return 0;
    }
    case WM_SIZE:
        resize_controls(hwnd);
        return 0;
    case WM_COMMAND:
        if (LOWORD(wp) == IDC_SEND) start_task();
        else if (LOWORD(wp) == IDC_BROWSE) browse_project();
        else if (LOWORD(wp) == IDC_NEW_PROJECT) new_project();
        else if (LOWORD(wp) == IDC_DELETE_PROJECT) delete_project();
        else if (LOWORD(wp) == IDC_RENAME_PROJECT) rename_project();
        else if (LOWORD(wp) == IDC_REFRESH_PROJECTS) refresh_projects();
        else if (LOWORD(wp) == IDC_PROJECTS && HIWORD(wp) == LBN_SELCHANGE) switch_project();
        else if (LOWORD(wp) == IDM_SETTINGS) show_settings();
        else if (LOWORD(wp) == IDM_UPDATE_CLIENT) update_client();
        return 0;
    case WM_ERASEBKGND:
        if (g_dark_mode) {
            RECT rc;
            GetClientRect(hwnd, &rc);
            FillRect((HDC)wp, &rc, g_dark_brush);
            return 1;
        }
        break;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
        {
            HBRUSH brush = control_color((HDC)wp, 1);
            if (brush) return (LRESULT)brush;
        }
        break;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
        {
            HBRUSH brush = control_color((HDC)wp, 0);
            if (brush) return (LRESULT)brush;
        }
        break;
    case WM_APPEND_TEXT: {
        char *text = (char *)lp;
        int end = GetWindowTextLength(g_transcript);
        if (end + (int)strlen(text) > TRANSCRIPT_LIMIT) {
            SendMessage(g_transcript, EM_SETSEL, 0, TRANSCRIPT_TRIM);
            SendMessage(g_transcript, EM_REPLACESEL, FALSE, (LPARAM)"");
            end = GetWindowTextLength(g_transcript);
        }
        SendMessage(g_transcript, EM_SETSEL, end, end);
        SendMessage(g_transcript, EM_REPLACESEL, FALSE, (LPARAM)text);
        SendMessage(g_transcript, EM_SCROLLCARET, 0, 0);
        save_chat();
        free(text);
        return 0;
    }
    case WM_SET_STATUS:
        SetWindowText(g_status, (char *)lp);
        free((void *)lp);
        return 0;
    case WM_TASK_DONE:
        g_busy = 0;
        EnableWindow(g_send, TRUE);
        EnableWindow(g_projects, TRUE);
        EnableWindow(GetDlgItem(hwnd, IDC_NEW_PROJECT), TRUE);
        EnableWindow(GetDlgItem(hwnd, IDC_DELETE_PROJECT), TRUE);
        EnableWindow(GetDlgItem(hwnd, IDC_RENAME_PROJECT), TRUE);
        {
            char root[MAX_PATH];
            GetWindowText(g_project, root, sizeof(root));
            set_project_status(root);
        }
        SetFocus(g_prompt);
        if (g_smoke) PostMessage(hwnd, WM_CLOSE, 0, 0);
        return 0;
    case WM_RUN_SMOKE:
        SetWindowText(g_prompt, "create hello");
        start_task();
        return 0;
    case WM_CLOSE:
        if (g_busy && MessageBox(hwnd, "Codex95 is still working. Close anyway?",
            APP_TITLE, MB_YESNO | MB_ICONQUESTION) != IDYES) return 0;
        save_chat();
        {
            Config cfg;
            controls_to_config(&cfg);
            save_config(&cfg);
        }
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR command_line, int show) {
    WNDCLASS wc;
    MSG msg;
    WSADATA wsa;
    HMENU menu, options;
    (void)previous;
    g_smoke = command_line && strstr(command_line, "/smoke") != NULL;
    ini_path();
    g_dark_brush = CreateSolidBrush(RGB(45, 47, 52));
    g_dark_edit_brush = CreateSolidBrush(RGB(32, 34, 38));
    g_dark_mode = GetPrivateProfileInt("Codex95", "DarkMode", 0, g_ini);
    g_text_scale = GetPrivateProfileInt("Codex95", "TextScale", 1, g_ini);
    if (g_text_scale < 0) g_text_scale = 0;
    if (g_text_scale > 4) g_text_scale = 4;
    g_ui_font = make_ui_font(g_text_scale);
    if (WSAStartup(MAKEWORD(1, 1), &wsa)) {
        MessageBox(NULL, "Winsock initialization failed.", APP_TITLE, MB_OK | MB_ICONSTOP);
        return 1;
    }
    memset(&wc, 0, sizeof(wc));
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = instance;
    wc.hIcon = LoadIcon(instance, MAKEINTRESOURCE(1));
    if (!wc.hIcon) wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = "Codex95Window";
    if (!RegisterClass(&wc)) return 1;
    wc.lpfnWndProc = settings_proc;
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = "Codex95Settings";
    if (!RegisterClass(&wc)) return 1;
    wc.lpfnWndProc = name_proc;
    wc.lpszClassName = "Codex95Name";
    if (!RegisterClass(&wc)) return 1;
    menu = CreateMenu();
    options = CreatePopupMenu();
    AppendMenu(options, MF_STRING, IDM_SETTINGS, "Settings...");
    AppendMenu(options, MF_STRING, IDM_UPDATE_CLIENT, "Update Codex95...");
    AppendMenu(menu, MF_POPUP, (UINT_PTR)options, "Options");
    g_main = CreateWindow("Codex95Window", APP_TITLE,
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 620, 430,
        NULL, menu, instance, NULL);
    if (!g_main) return 1;
    ShowWindow(g_main, show);
    UpdateWindow(g_main);
    if (g_smoke) PostMessage(g_main, WM_RUN_SMOKE, 0, 0);
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    WSACleanup();
    if (g_ui_font) DeleteObject(g_ui_font);
    DeleteObject(g_dark_edit_brush);
    DeleteObject(g_dark_brush);
    return (int)msg.wParam;
}

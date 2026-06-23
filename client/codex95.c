/*
 * Codex95 thin client
 *
 * Target: Visual C++ 6.0 / Win32 / Winsock 1.1
 * Build:  cl /O1 /W3 codex95.c wsock32.lib
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define BUF_SIZE 4096
#define BIG_SIZE 49152
#define RESULT_SIZE 32000

#ifndef CP_UTF8
#define CP_UTF8 65001
#endif

typedef struct {
    char host[128];
    int port;
    char root[MAX_PATH];
    char device_name[128];
    char model[80];
    int auto_yes;
    int full_access;
} Config;

static const char b64_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

static void trim_newline(char *s) {
    size_t n = strlen(s);
    while (n && (s[n - 1] == '\r' || s[n - 1] == '\n')) s[--n] = 0;
}

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

static void utf8_put(unsigned int code, char *dst, size_t cap, size_t *used) {
    if (code < 0x80) {
        if (*used + 1 < cap) dst[(*used)++] = (char)code;
    } else if (code < 0x800) {
        if (*used + 2 < cap) {
            dst[(*used)++] = (char)(0xC0 | (code >> 6));
            dst[(*used)++] = (char)(0x80 | (code & 0x3F));
        }
    } else {
        if (*used + 3 < cap) {
            dst[(*used)++] = (char)(0xE0 | (code >> 12));
            dst[(*used)++] = (char)(0x80 | ((code >> 6) & 0x3F));
            dst[(*used)++] = (char)(0x80 | (code & 0x3F));
        }
    }
}

static void cp1251_to_utf8_fallback(const char *src, char *dst, size_t cap) {
    size_t used = 0;
    while (*src && used + 1 < cap) {
        unsigned char c = (unsigned char)*src++;
        unsigned int code = c;
        if (c >= 0xC0) code = 0x0410 + (c - 0xC0);
        else if (c == 0xA8) code = 0x0401;
        else if (c == 0xB8) code = 0x0451;
        else if (c >= 0x80) code = '?';
        utf8_put(code, dst, cap, &used);
    }
    dst[used] = 0;
}

static int utf8_next(const unsigned char **src) {
    const unsigned char *s = *src;
    int code;
    if (!*s) return 0;
    if (*s < 0x80) {
        (*src)++;
        return *s;
    }
    if ((*s & 0xE0) == 0xC0 && s[1]) {
        code = ((*s & 0x1F) << 6) | (s[1] & 0x3F);
        *src += 2;
        return code;
    }
    if ((*s & 0xF0) == 0xE0 && s[1] && s[2]) {
        code = ((*s & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
        *src += 3;
        return code;
    }
    (*src)++;
    return '?';
}

static void utf8_to_cp1251_fallback(const char *src, char *dst, size_t cap) {
    const unsigned char *p = (const unsigned char *)src;
    size_t used = 0;
    while (*p && used + 1 < cap) {
        int code = utf8_next(&p);
        if (code < 0x80) dst[used++] = (char)code;
        else if (code >= 0x0410 && code <= 0x044F) dst[used++] = (char)(0xC0 + code - 0x0410);
        else if (code == 0x0401) dst[used++] = (char)0xA8;
        else if (code == 0x0451) dst[used++] = (char)0xB8;
        else dst[used++] = '?';
    }
    dst[used] = 0;
}

static void local_to_utf8(const char *src, char *dst, size_t cap) {
    int wlen, ok;
    WCHAR *wide;
    if (!src || !dst || !cap) return;
    wlen = MultiByteToWideChar(CP_ACP, 0, src, -1, NULL, 0);
    if (wlen > 0) {
        wide = (WCHAR *)malloc(sizeof(WCHAR) * wlen);
        if (wide) {
            MultiByteToWideChar(CP_ACP, 0, src, -1, wide, wlen);
            ok = WideCharToMultiByte(CP_UTF8, 0, wide, -1, dst, (int)cap, NULL, NULL);
            free(wide);
            if (ok > 0) return;
        }
    }
    if (GetACP() == 1251) cp1251_to_utf8_fallback(src, dst, cap);
    else {
        strncpy(dst, src, cap - 1);
        dst[cap - 1] = 0;
    }
}

static void utf8_to_local(const char *src, char *dst, size_t cap) {
    int wlen, ok;
    WCHAR *wide;
    if (!src || !dst || !cap) return;
    wlen = MultiByteToWideChar(CP_UTF8, 0, src, -1, NULL, 0);
    if (wlen > 0) {
        wide = (WCHAR *)malloc(sizeof(WCHAR) * wlen);
        if (wide) {
            MultiByteToWideChar(CP_UTF8, 0, src, -1, wide, wlen);
            ok = WideCharToMultiByte(CP_ACP, 0, wide, -1, dst, (int)cap, NULL, NULL);
            free(wide);
            if (ok > 0) return;
        }
    }
    if (GetACP() == 1251) utf8_to_cp1251_fallback(src, dst, cap);
    else {
        strncpy(dst, src, cap - 1);
        dst[cap - 1] = 0;
    }
}

static void convert_inplace_utf8_to_local(char *text, size_t cap) {
    char *tmp = (char *)malloc(cap);
    if (!tmp) return;
    utf8_to_local(text, tmp, cap);
    strncpy(text, tmp, cap - 1);
    text[cap - 1] = 0;
    free(tmp);
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

static int resolve_path(const Config *cfg, const char *relative, char *out) {
    char combined[MAX_PATH * 2];
    char root[MAX_PATH];
    size_t root_len;
    GetFullPathName(cfg->root, MAX_PATH, root, NULL);
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

static int confirm(const Config *cfg, const char *verb, const char *detail) {
    char line[16];
    if (cfg->auto_yes) return 1;
    printf("\nAllow %s: %s ? [y/N] ", verb, detail);
    fflush(stdout);
    if (!fgets(line, sizeof(line), stdin)) return 0;
    return line[0] == 'y' || line[0] == 'Y';
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
    if (h == INVALID_HANDLE_VALUE) {
        strcpy(out, "ERROR: directory not found");
        return;
    }
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
    if (!CreateProcess(NULL, cmd, NULL, NULL, TRUE, 0,
                       NULL, cfg->root, &si, &pi)) {
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
    if (wait == WAIT_TIMEOUT) {
        DeleteFile(full_temp);
        return;
    }
    f = fopen(full_temp, "rb");
    if (!f) {
        sprintf(out, "Exit code: %lu", code);
        DeleteFile(full_temp);
        return;
    }
    n = fread(out, 1, cap - 64, f);
    fclose(f);
    DeleteFile(full_temp);
    out[n] = 0;
    sprintf(out + n, "\r\n[exit=%lu]", code);
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
    char found[MAX_PATH], line[MAX_PATH + 32];
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
    if (!found_count) profile_append(out, cap, "(none found)\r\n");
}

static void execute_action(const Config *cfg, const char *reply, char *result, size_t cap) {
    char action[64], path[MAX_PATH], command[BUF_SIZE];
    char full[MAX_PATH];
    field(reply, "action", action, sizeof(action));
    field(reply, "path", path, sizeof(path));
    convert_inplace_utf8_to_local(path, sizeof(path));

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
        else if (!confirm(cfg, "write_file", path)) strcpy(result, "DENIED by user");
        else {
            len = b64_decode(data, decoded, RESULT_SIZE);
            write_file(full, decoded, len, result);
        }
        free(decoded);
        free(data);
    } else if (!strcmp(action, "make_dir")) {
        if (!resolve_path(cfg, path, full)) strcpy(result, "ERROR: path outside project root");
        else if (!confirm(cfg, "make_dir", path)) strcpy(result, "DENIED by user");
        else make_dir(full, result);
    } else if (!strcmp(action, "delete_path")) {
        if (!resolve_path(cfg, path, full)) strcpy(result, "ERROR: path outside allowed access");
        else if (strlen(full) <= 3) strcpy(result, "ERROR: refusing to delete a drive root");
        else if (!confirm(cfg, "delete_path", full)) strcpy(result, "DENIED by user");
        else if (delete_tree(full)) strcpy(result, "OK: deleted");
        else sprintf(result, "ERROR: deletion failed (%lu)", GetLastError());
    } else if (!strcmp(action, "run_command")) {
        field(reply, "command", command, sizeof(command));
        convert_inplace_utf8_to_local(command, sizeof(command));
        if (!confirm(cfg, "run_command", command)) strcpy(result, "DENIED by user");
        else run_command(cfg, command, result, cap);
    } else if (!strcmp(action, "run_program")) {
        field(reply, "command", command, sizeof(command));
        convert_inplace_utf8_to_local(command, sizeof(command));
        if (!confirm(cfg, "run_program", command)) strcpy(result, "DENIED by user");
        else run_program(cfg, command, result);
    } else if (!strcmp(action, "system_info")) {
        device_profile(cfg, result, cap);
    } else {
        sprintf(result, "ERROR: unknown action '%s'", action);
    }
}

static void usage(void) {
    puts("Codex95 client");
    puts("Usage: CODEX95.EXE <bridge-host> [port] [project-root] [-y] [-full]");
    puts("Example: CODEX95.EXE 192.168.1.50 8787 C:\\DEV\\CLOCK");
}

int main(int argc, char **argv) {
    Config cfg;
    WSADATA wsa;
    char prompt[BUF_SIZE], prompt_enc[BIG_SIZE], root_enc[BUF_SIZE];
    char body[BIG_SIZE], reply[BIG_SIZE], session[128], status[64];
    char message_b64[BIG_SIZE], message[BIG_SIZE], result[RESULT_SIZE];
    char result_b64[BIG_SIZE];
    char prompt_utf8[BIG_SIZE], root_utf8[BUF_SIZE];
    char *result_utf8;

    if (argc < 2) { usage(); return 1; }
    memset(&cfg, 0, sizeof(cfg));
    strncpy(cfg.host, argv[1], sizeof(cfg.host) - 1);
    cfg.port = argc > 2 ? atoi(argv[2]) : 8787;
    strncpy(cfg.root, argc > 3 ? argv[3] : "C:\\CODEX95\\WORK", sizeof(cfg.root) - 1);
    strcpy(cfg.device_name, "Toshiba Libretto 70CT");
    if (!GetEnvironmentVariable("CODEX95_MODEL", cfg.model, sizeof(cfg.model)))
        strcpy(cfg.model, "gpt-5.4-mini");
    cfg.auto_yes = (argc > 4 && !strcmp(argv[4], "-y")) || (argc > 5 && !strcmp(argv[5], "-y"));
    cfg.full_access = (argc > 4 && !strcmp(argv[4], "-full")) || (argc > 5 && !strcmp(argv[5], "-full"));
    if (WSAStartup(MAKEWORD(1, 1), &wsa)) {
        puts("Winsock initialization failed.");
        return 1;
    }

    printf("Codex95 connected to %s:%d\nProject: %s\n", cfg.host, cfg.port, cfg.root);
    for (;;) {
        printf("\n> ");
        fflush(stdout);
        if (!fgets(prompt, sizeof(prompt), stdin)) break;
        trim_newline(prompt);
        if (!strcmp(prompt, "/quit") || !strcmp(prompt, "/exit")) break;
        if (!prompt[0]) continue;

        local_to_utf8(prompt, prompt_utf8, sizeof(prompt_utf8));
        local_to_utf8(cfg.root, root_utf8, sizeof(root_utf8));
        url_encode(prompt_utf8, prompt_enc, sizeof(prompt_enc));
        url_encode(root_utf8, root_enc, sizeof(root_enc));
        {
            char profile[BUF_SIZE], profile_utf8[BIG_SIZE], profile_b64[BUF_SIZE * 2], model_enc[256];
            device_profile(&cfg, profile, sizeof(profile));
            local_to_utf8(profile, profile_utf8, sizeof(profile_utf8));
            b64_encode((unsigned char *)profile_utf8, strlen(profile_utf8), profile_b64, sizeof(profile_b64));
            url_encode(cfg.model, model_enc, sizeof(model_enc));
            _snprintf(body, sizeof(body) - 1, "prompt=%s&root=%s&profile=%s&access=%s&model=%s",
                prompt_enc, root_enc, profile_b64, cfg.full_access ? "full" : "project",
                model_enc);
        }
        body[sizeof(body) - 1] = 0;
        if (!http_post(&cfg, "/session/start", body, reply, sizeof(reply))) {
            puts("Bridge connection failed.");
            continue;
        }

        for (;;) {
            field(reply, "status", status, sizeof(status));
            field(reply, "session", session, sizeof(session));
            if (!strcmp(status, "message")) {
                field(reply, "message", message_b64, sizeof(message_b64));
                b64_decode(message_b64, (unsigned char *)message, sizeof(message));
                convert_inplace_utf8_to_local(message, sizeof(message));
                printf("\n%s\n", message);
                break;
            }
            if (!strcmp(status, "error")) {
                field(reply, "message", message_b64, sizeof(message_b64));
                b64_decode(message_b64, (unsigned char *)message, sizeof(message));
                convert_inplace_utf8_to_local(message, sizeof(message));
                printf("\nERROR: %s\n", message);
                break;
            }
            if (strcmp(status, "action")) {
                puts("Invalid bridge response.");
                break;
            }

            execute_action(&cfg, reply, result, sizeof(result));
            result_utf8 = (char *)malloc(BIG_SIZE);
            if (result_utf8) {
                local_to_utf8(result, result_utf8, BIG_SIZE);
                b64_encode((unsigned char *)result_utf8, strlen(result_utf8), result_b64, sizeof(result_b64));
                free(result_utf8);
            } else {
                b64_encode((unsigned char *)result, strlen(result), result_b64, sizeof(result_b64));
            }
            _snprintf(body, sizeof(body) - 1, "session=%s&result=%s", session, result_b64);
            body[sizeof(body) - 1] = 0;
            if (!http_post(&cfg, "/session/continue", body, reply, sizeof(reply))) {
                puts("Bridge connection failed while returning action result.");
                break;
            }
        }
    }
    WSACleanup();
    return 0;
}

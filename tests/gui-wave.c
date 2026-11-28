// Exercise the GUI's Wave Out menu, file picker and TX button in an empty working directory.
// Build as a Win32 console program with MinGW or MSVC. Pass the path to EasyDRF.exe.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dlgs.h>
#include <mmsystem.h>
#include "../resource.h"

static PROCESS_INFORMATION app;
static HWND match;
static int kind;

static BOOL CALLBACK find_dialog(HWND window, LPARAM unused)
{
    DWORD pid;
    char title[160] = "";
    GetWindowThreadProcessId(window, &pid);
    if (pid != app.dwProcessId || !IsWindowVisible(window)) return TRUE;
    GetWindowTextA(window, title, sizeof title);
    if ((kind == 0 && GetDlgItem(window, IDB_STARTPIC)) ||
        (kind == 1 && !strcmp(title, "Select Files")) ||
        (kind == 2 && (GetDlgItem(window, edt1) || GetDlgItem(window, cmb13)))) {
        match = window;
        return FALSE;
    }
    if (kind == 3) fprintf(stderr, "Visible window: %s\n", title);
    return TRUE;
}

static void fail(const char *why)
{
    DWORD code;
    fprintf(stderr, "GUI test: %s\n", why);
    kind = 3;
    EnumWindows(find_dialog, 0);
    if (GetExitCodeProcess(app.hProcess, &code))
        fprintf(stderr, "EasyDRF process status 0x%08lx\n", (unsigned long)code);
    TerminateProcess(app.hProcess, 1);
    exit(1);
}

static BOOL CALLBACK find_edit(HWND window, LPARAM result)
{
    char name[32] = "";
    GetClassNameA(window, name, sizeof name);
    if (lstrcmpiA(name, "Edit") != 0) return TRUE;
    *(HWND*)result = window;
    return FALSE;
}

static HWND wait_dialog(int target)
{
    kind = target;
    for (int i = 0; i < 250; ++i) {
        match = NULL;
        EnumWindows(find_dialog, 0);
        if (match) return match;
        if (WaitForSingleObject(app.hProcess, 100) == WAIT_OBJECT_0) break;
    }
    fail("expected dialog did not open");
    return NULL;
}

static int complete_wave(void)
{
    WIN32_FIND_DATAA info;
    HANDLE search = FindFirstFileA("WaveOut\\*.wav", &info);
    if (search == INVALID_HANDLE_VALUE) return 0;
    FindClose(search);
    char path[MAX_PATH];
    snprintf(path, sizeof path, "WaveOut\\%s", info.cFileName);
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    unsigned char h[44];
    unsigned size = 0;
    if (fread(h, 1, sizeof h, f) == sizeof h && !memcmp(h, "RIFF", 4) &&
            !memcmp(h + 36, "data", 4)) memcpy(&size, h + 40, 4);
    fclose(f);
    return size > 0 && size + 44 == info.nFileSizeLow;
}

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    setvbuf(stdout, NULL, _IONBF, 0);
    CreateDirectoryA("Rx Files", NULL);
    CreateDirectoryA("Corrupt", NULL);
    CreateDirectoryA("WaveOut", NULL);
    FILE *payload = fopen("cassette.bin", "wb");
    if (!payload) return 2;
    fputs("A normal EasyDRF cassette file.\n", payload);
    fclose(payload);
    STARTUPINFOA si = {sizeof si};
    if (!CreateProcessA(argv[1], NULL, NULL, NULL, FALSE, 0, NULL, NULL, &si, &app)) return 2;
    HWND main = wait_dialog(0);
    WaitForInputIdle(app.hProcess, 15000);
    int command = IDM_O_TX_O_DRIVERS0 + waveOutGetNumDevs();
    printf("Selecting Wave Out (menu ID %d)\n", command);
    SendMessageA(main, WM_COMMAND, command, 0);
    PostMessageA(main, WM_COMMAND, IDB_STARTPIC, 0);
    HWND select = wait_dialog(1);
    PostMessageA(select, WM_COMMAND, ID_CHOOSEFILE, 0);
    HWND open = wait_dialog(2);
    char full[MAX_PATH];
    GetFullPathNameA("cassette.bin", MAX_PATH, full, NULL);
    HWND edit = GetDlgItem(open, edt1);
    if (!edit) EnumChildWindows(GetDlgItem(open, cmb13), find_edit, (LPARAM)&edit);
    if (!edit) EnumChildWindows(open, find_edit, (LPARAM)&edit);
    if (!edit) fail("file picker has no filename field");
    SendMessageA(edit, WM_SETTEXT, 0, (LPARAM)full);
    PostMessageA(open, WM_COMMAND, IDOK, 0);
    char list[256] = "";
    for (int i = 0; i < 100 && !strstr(list, "cassette.bin"); ++i) {
        Sleep(100);
        SendMessageA(GetDlgItem(select, IDC_PICFILE_TX), WM_GETTEXT, sizeof list, (LPARAM)list);
    }
    if (!strstr(list, "cassette.bin")) fail("file picker did not queue the file");
    puts("Clicking TX with cassette.bin queued");
    PostMessageA(select, WM_COMMAND, IDOK, 0);
    int done = 0;
    for (int i = 0; i < 300 && !done; ++i) {
        if (WaitForSingleObject(app.hProcess, 100) == WAIT_OBJECT_0) fail("recording terminated EasyDRF");
        done = complete_wave();
    }
    if (!done) fail("a finalized recording did not appear");
    puts("The GUI finalized a WAV");
    PostMessageA(main, WM_CLOSE, 0, 0);
    WaitForSingleObject(app.hProcess, 10000);
    CloseHandle(app.hProcess);
    CloseHandle(app.hThread);
    return 0;
}

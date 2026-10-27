/* Bloodborne.exe: Native Windows launcher and role dispatcher for Bloodborne PC port.
 * Completely replaces the bloated 6MB PyInstaller binary without needing _internal.
 * Supports:
 *   - Bloodborne.exe (no arguments: launches direct splash/game)
 *   - Bloodborne.exe --launcher (opens GUI settings launcher)
 *   - Bloodborne.exe --play (starts game directly)
 *   - Bloodborne.exe --run [args...] (dispatches to run.py)
 *   - Bloodborne.exe --script <script> [args...] (runs helper script)
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <stdio.h>
#include <wchar.h>

static void get_app_dir(wchar_t *dir, size_t max_len) {
    DWORD len = GetModuleFileNameW(NULL, dir, max_len);
    if (len > 0 && len < max_len) {
        wchar_t *slash = wcsrchr(dir, L'\\');
        if (slash) *slash = 0;
    }
}

static BOOL find_python(const wchar_t *appdata, const wchar_t *dir, BOOL windowless, wchar_t *out_py) {
    const wchar_t *exe_name = windowless ? L"pythonw.exe" : L"python.exe";
    const wchar_t *alt_exe = windowless ? L"python.exe" : L"pythonw.exe";
    const wchar_t *vers[] = { L"Python313", L"Python314", L"Python312", L"Python311", L"Python310" };

    /* 1. AppData Programs Python */
    if (appdata && appdata[0]) {
        for (int i = 0; i < 5; i++) {
            swprintf(out_py, 32768, L"%ls\\Programs\\Python\\%ls\\%ls", appdata, vers[i], exe_name);
            if (GetFileAttributesW(out_py) != INVALID_FILE_ATTRIBUTES) return TRUE;
            swprintf(out_py, 32768, L"%ls\\Programs\\Python\\%ls\\%ls", appdata, vers[i], alt_exe);
            if (GetFileAttributesW(out_py) != INVALID_FILE_ATTRIBUTES) return TRUE;
        }
        /* 2. AppData Bloodborne runtime */
        swprintf(out_py, 32768, L"%ls\\Bloodborne\\runtime\\%ls", appdata, exe_name);
        if (GetFileAttributesW(out_py) != INVALID_FILE_ATTRIBUTES) return TRUE;
        swprintf(out_py, 32768, L"%ls\\Bloodborne\\runtime\\%ls", appdata, alt_exe);
        if (GetFileAttributesW(out_py) != INVALID_FILE_ATTRIBUTES) return TRUE;
    }

    /* 3. Local _internal */
    swprintf(out_py, 32768, L"%ls\\_internal\\%ls", dir, exe_name);
    if (GetFileAttributesW(out_py) != INVALID_FILE_ATTRIBUTES) return TRUE;

    /* 4. PATH fallback */
    wcscpy(out_py, exe_name);
    return TRUE;
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR arguments, int show) {
    (void)instance; (void)previous; (void)show;

    wchar_t dir[32768], appdata[32768];
    get_app_dir(dir, 32768);
    GetEnvironmentVariableW(L"LOCALAPPDATA", appdata, 32768);

    /* If run from root, check if BloodBorne Port subfolder exists */
    wchar_t port_dir[32768];
    swprintf(port_dir, 32768, L"%ls\\BloodBorne Port", dir);
    if (GetFileAttributesW(port_dir) == INVALID_FILE_ATTRIBUTES) {
        wcscpy(port_dir, dir);
    }

    /* Analyze arguments to determine target script and mode */
    BOOL is_run = FALSE;
    BOOL is_script = FALSE;
    wchar_t script_target[32768] = L"";
    wchar_t extra_args[32768] = L"";

    if (arguments && *arguments) {
        if (wcsstr(arguments, L"--run") != NULL) {
            is_run = TRUE;
            const wchar_t *p = wcsstr(arguments, L"--run");
            p += 5;
            while (*p == L' ') p++;
            wcscpy(extra_args, p);
        } else if (wcsstr(arguments, L"--script") != NULL) {
            is_script = TRUE;
            const wchar_t *p = wcsstr(arguments, L"--script");
            p += 8;
            while (*p == L' ') p++;
            /* Extract script path and remaining args */
            if (*p == L'\"') {
                p++;
                const wchar_t *end = wcschr(p, L'\"');
                if (end) {
                    wcsncpy(script_target, p, end - p);
                    script_target[end - p] = 0;
                    p = end + 1;
                    while (*p == L' ') p++;
                    wcscpy(extra_args, p);
                }
            } else {
                const wchar_t *end = wcschr(p, L' ');
                if (end) {
                    wcsncpy(script_target, p, end - p);
                    script_target[end - p] = 0;
                    p = end + 1;
                    while (*p == L' ') p++;
                    wcscpy(extra_args, p);
                } else {
                    wcscpy(script_target, p);
                }
            }
        }
    }

    /* Locate Python */
    BOOL windowless = !(is_run || is_script);
    wchar_t python[32768];
    find_python(appdata, port_dir, windowless, python);

    /* Build command line */
    wchar_t cmd[65536];
    if (is_run) {
        wchar_t run_py[32768];
        swprintf(run_py, 32768, L"%ls\\run.py", port_dir);
        swprintf(cmd, 65536, L"\"%ls\" \"%ls\"%ls%ls", python, run_py,
                 extra_args[0] ? L" " : L"", extra_args);
    } else if (is_script) {
        swprintf(cmd, 65536, L"\"%ls\" \"%ls\"%ls%ls", python, script_target,
                 extra_args[0] ? L" " : L"", extra_args);
    } else {
        wchar_t launcher_py[32768];
        swprintf(launcher_py, 32768, L"%ls\\launcher\\bbport_launcher_win.py", port_dir);
        swprintf(cmd, 65536, L"\"%ls\" \"%ls\"%ls%ls", python, launcher_py,
                 arguments && *arguments ? L" " : L"", arguments ? arguments : L"");
    }

    STARTUPINFOW si = {.cb = sizeof(si)};
    PROCESS_INFORMATION pi;
    DWORD creation_flags = 0;

    if (!CreateProcessW(NULL, cmd, NULL, NULL, TRUE, creation_flags, NULL, port_dir, &si, &pi)) {
        DWORD err = GetLastError();
        wchar_t msg[1024];
        swprintf(msg, 1024,
            L"Could not launch Bloodborne.\n\n"
            L"Command: %ls\nError code: %lu\n\n"
            L"Please run 'Setup Bloodborne.exe' to verify your installation.",
            cmd, (unsigned long)err);
        MessageBoxW(NULL, msg, L"Bloodborne", MB_OK | MB_ICONERROR);
        return 1;
    }

    if (is_run || is_script) {
        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD code = 0;
        GetExitCodeProcess(pi.hProcess, &code);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return (int)code;
    }

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return 0;
}

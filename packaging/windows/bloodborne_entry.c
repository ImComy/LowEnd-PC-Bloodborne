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

static BOOL check_py_path(const wchar_t *candidate, wchar_t *out_py) {
    if (!candidate || !*candidate) return FALSE;
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!GetFileAttributesExW(candidate, GetFileExInfoStandard, &fad)) return FALSE;
    if (fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) return FALSE;
    /* Microsoft Store execution aliases in WindowsApps are 0 bytes or dummy stubs */
    if (fad.nFileSizeHigh == 0 && fad.nFileSizeLow < 50000) return FALSE;
    /* Reject WindowsApps folder stubs */
    if (wcsstr(candidate, L"\\WindowsApps\\") || wcsstr(candidate, L"\\windowsapps\\")) return FALSE;
    wcscpy(out_py, candidate);
    return TRUE;
}

static BOOL check_registry_python(HKEY root, const wchar_t *subver, const wchar_t *exe_name, wchar_t *out_py) {
    wchar_t subkey[512];
    swprintf(subkey, 512, L"Software\\Python\\PythonCore\\%ls\\InstallPath", subver);
    HKEY hkey;
    if (RegOpenKeyExW(root, subkey, 0, KEY_READ, &hkey) == ERROR_SUCCESS) {
        wchar_t install_dir[MAX_PATH];
        DWORD size = sizeof(install_dir);
        DWORD type = 0;
        if (RegQueryValueExW(hkey, NULL, NULL, &type, (LPBYTE)install_dir, &size) == ERROR_SUCCESS) {
            wchar_t candidate[32768];
            swprintf(candidate, 32768, L"%ls\\%ls", install_dir, exe_name);
            RegCloseKey(hkey);
            if (check_py_path(candidate, out_py)) return TRUE;
        } else {
            RegCloseKey(hkey);
        }
    }
    return FALSE;
}

static BOOL check_search_path(const wchar_t *exe_name, wchar_t *out_py) {
    wchar_t resolved[32768];
    DWORD len = SearchPathW(NULL, exe_name, NULL, 32768, resolved, NULL);
    if (len > 0 && len < 32768) {
        if (check_py_path(resolved, out_py)) {
            return TRUE;
        }
    }
    return FALSE;
}

static BOOL find_python_exe(const wchar_t *appdata, const wchar_t *dir, const wchar_t *exe_name, wchar_t *out_py) {
    wchar_t cand[32768];
    const wchar_t *vers[] = { L"Python314", L"Python313", L"Python312", L"Python311", L"Python310" };
    const wchar_t *reg_vers[] = { L"3.14", L"3.13", L"3.12", L"3.11", L"3.10" };

    /* 1. Local _internal */
    swprintf(cand, 32768, L"%ls\\_internal\\%ls", dir, exe_name);
    if (check_py_path(cand, out_py)) return TRUE;

    /* 2. AppData Bloodborne runtime */
    if (appdata && appdata[0]) {
        swprintf(cand, 32768, L"%ls\\Bloodborne\\runtime\\%ls", appdata, exe_name);
        if (check_py_path(cand, out_py)) return TRUE;

        /* 3. AppData Programs Python */
        for (int i = 0; i < 5; i++) {
            swprintf(cand, 32768, L"%ls\\Programs\\Python\\%ls\\%ls", appdata, vers[i], exe_name);
            if (check_py_path(cand, out_py)) return TRUE;
        }
    }

    /* 4. Program Files */
    wchar_t pf[32768];
    if (GetEnvironmentVariableW(L"ProgramFiles", pf, 32768) > 0) {
        for (int i = 0; i < 5; i++) {
            swprintf(cand, 32768, L"%ls\\%ls\\%ls", pf, vers[i], exe_name);
            if (check_py_path(cand, out_py)) return TRUE;
        }
    }
    if (GetEnvironmentVariableW(L"ProgramFiles(x86)", pf, 32768) > 0) {
        for (int i = 0; i < 5; i++) {
            swprintf(cand, 32768, L"%ls\\%ls\\%ls", pf, vers[i], exe_name);
            if (check_py_path(cand, out_py)) return TRUE;
        }
    }

    /* 5. C:\Python3* */
    for (int i = 0; i < 5; i++) {
        swprintf(cand, 32768, L"C:\\%ls\\%ls", vers[i], exe_name);
        if (check_py_path(cand, out_py)) return TRUE;
    }

    /* 6. Windows Registry (HKCU then HKLM) */
    for (int i = 0; i < 5; i++) {
        if (check_registry_python(HKEY_CURRENT_USER, reg_vers[i], exe_name, out_py)) return TRUE;
        if (check_registry_python(HKEY_LOCAL_MACHINE, reg_vers[i], exe_name, out_py)) return TRUE;
    }

    /* 7. PATH via SearchPathW (skipping WindowsApps stubs) */
    if (check_search_path(exe_name, out_py)) return TRUE;

    return FALSE;
}

static BOOL find_python(const wchar_t *appdata, const wchar_t *dir, BOOL windowless, wchar_t *out_py) {
    const wchar_t *pri_exe = windowless ? L"pythonw.exe" : L"python.exe";
    const wchar_t *alt_exe = windowless ? L"python.exe" : L"pythonw.exe";

    if (find_python_exe(appdata, dir, pri_exe, out_py)) return TRUE;
    if (find_python_exe(appdata, dir, alt_exe, out_py)) return TRUE;
    return FALSE;
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
    if (!find_python(appdata, port_dir, windowless, python)) {
        MessageBoxW(NULL,
            L"Python 3.10 or newer is required to run Bloodborne PC.\n\n"
            L"Could not locate a valid Python installation on this system.\n"
            L"Please install Python 3.10+ from https://www.python.org/ (ensure 'Add Python to PATH' is checked),\n"
            L"or run 'Setup Bloodborne.exe' to configure the environment.",
            L"Bloodborne PC - Python Required",
            MB_OK | MB_ICONERROR);
        return 1;
    }

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

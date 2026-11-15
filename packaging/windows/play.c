/* Play Bloodborne.exe: starts the game with the settings saved by the launcher, without opening
 * the launcher (it runs `Bloodborne.exe --play` from its own folder and waits for it, so Steam
 * and other front ends see the game running). Extra arguments are passed on. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <wchar.h>

static BOOL check_py_path(const wchar_t *candidate, wchar_t *out_py) {
    if (!candidate || !*candidate) return FALSE;
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!GetFileAttributesExW(candidate, GetFileExInfoStandard, &fad)) return FALSE;
    if (fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) return FALSE;
    if (fad.nFileSizeHigh == 0 && fad.nFileSizeLow < 50000) return FALSE;
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
        if (check_py_path(resolved, out_py)) return TRUE;
    }
    return FALSE;
}

static BOOL find_python_game(const wchar_t *appdata, const wchar_t *dir, wchar_t *out_py) {
    const wchar_t *exes[] = { L"python.exe", L"pythonw.exe" };
    const wchar_t *vers[] = { L"Python314", L"Python313", L"Python312", L"Python311", L"Python310" };
    const wchar_t *reg_vers[] = { L"3.14", L"3.13", L"3.12", L"3.11", L"3.10" };
    wchar_t cand[32768];

    for (int e = 0; e < 2; e++) {
        /* 1. Local _internal */
        swprintf(cand, 32768, L"%ls\\_internal\\%ls", dir, exes[e]);
        if (check_py_path(cand, out_py)) return TRUE;

        /* 2. AppData Bloodborne runtime */
        if (appdata && appdata[0]) {
            swprintf(cand, 32768, L"%ls\\Bloodborne\\runtime\\%ls", appdata, exes[e]);
            if (check_py_path(cand, out_py)) return TRUE;

            /* 3. AppData Programs Python */
            for (int i = 0; i < 5; i++) {
                swprintf(cand, 32768, L"%ls\\Programs\\Python\\%ls\\%ls", appdata, vers[i], exes[e]);
                if (check_py_path(cand, out_py)) return TRUE;
            }
        }

        /* 4. Program Files */
        wchar_t pf[32768];
        if (GetEnvironmentVariableW(L"ProgramFiles", pf, 32768) > 0) {
            for (int i = 0; i < 5; i++) {
                swprintf(cand, 32768, L"%ls\\%ls\\%ls", pf, vers[i], exes[e]);
                if (check_py_path(cand, out_py)) return TRUE;
            }
        }
        if (GetEnvironmentVariableW(L"ProgramFiles(x86)", pf, 32768) > 0) {
            for (int i = 0; i < 5; i++) {
                swprintf(cand, 32768, L"%ls\\%ls\\%ls", pf, vers[i], exes[e]);
                if (check_py_path(cand, out_py)) return TRUE;
            }
        }

        /* 5. C:\Python3* */
        for (int i = 0; i < 5; i++) {
            swprintf(cand, 32768, L"C:\\%ls\\%ls", vers[i], exes[e]);
            if (check_py_path(cand, out_py)) return TRUE;
        }

        /* 6. Registry */
        for (int i = 0; i < 5; i++) {
            if (check_registry_python(HKEY_CURRENT_USER, reg_vers[i], exes[e], out_py)) return TRUE;
            if (check_registry_python(HKEY_LOCAL_MACHINE, reg_vers[i], exes[e], out_py)) return TRUE;
        }

        /* 7. SearchPath */
        if (check_search_path(exes[e], out_py)) return TRUE;
    }
    return FALSE;
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR arguments, int show)
{
    (void)instance, (void)previous, (void)show;
    static wchar_t dir[32768], exe[32768], parameters[32768], line[32768], text[1024];
    DWORD length = GetModuleFileNameW(NULL, dir, 32768);
    if (length == 0 || length >= 32768)
        return 1;
    wchar_t *slash = wcsrchr(dir, L'\\');
    if (slash)
        *slash = 0;
    swprintf(exe, 32768, L"%ls\\Bloodborne.exe", dir);
    swprintf(parameters, 32768, L"--play%ls%ls", arguments && *arguments ? L" " : L"",
             arguments ? arguments : L"");
    swprintf(line, 32768, L"\"%ls\" %ls", exe, parameters);

    HANDLE game = NULL;
    STARTUPINFOW startup = {.cb = sizeof(startup)};
    PROCESS_INFORMATION process;
    DWORD error = 0;
    if (CreateProcessW(exe, line, NULL, NULL, FALSE, 0, NULL, dir, &startup, &process)) {
        CloseHandle(process.hThread);
        game = process.hProcess;
    } else {
        error = GetLastError();
        if (error == ERROR_ELEVATION_REQUIRED) {
            /* Bloodborne.exe is set to "Run this program as an administrator" (its Compatibility
             * properties): CreateProcess refuses that, only the shell can ask for it. */
            SHELLEXECUTEINFOW shell = {.cbSize = sizeof(shell), .fMask = SEE_MASK_NOCLOSEPROCESS,
                                       .lpFile = exe, .lpParameters = parameters,
                                       .lpDirectory = dir, .nShow = SW_SHOWNORMAL};
            if (ShellExecuteExW(&shell) && shell.hProcess)
                game = shell.hProcess;
            else
                error = GetLastError();
        }
    }
    if (!game) {
        /* Fallback: run run.py with python */
        static wchar_t py_script[32768];
        swprintf(py_script, 32768, L"%ls\\run.py", dir);
        if (GetFileAttributesW(py_script) == INVALID_FILE_ATTRIBUTES) {
            swprintf(py_script, 32768, L"%ls\\BloodBorne Port\\run.py", dir);
            if (GetFileAttributesW(py_script) != INVALID_FILE_ATTRIBUTES)
                swprintf(dir, 32768, L"%ls\\BloodBorne Port", dir);
        }
        if (GetFileAttributesW(py_script) != INVALID_FILE_ATTRIBUTES) {
            wchar_t appdata[32768] = L"";
            GetEnvironmentVariableW(L"LOCALAPPDATA", appdata, 32768);
            wchar_t py_exe[32768];
            if (find_python_game(appdata, dir, py_exe)) {
                swprintf(parameters, 32768, L"\"%ls\"%ls%ls", py_script,
                         arguments && *arguments ? L" " : L"", arguments ? arguments : L"");
                swprintf(line, 32768, L"\"%ls\" %ls", py_exe, parameters);

                STARTUPINFOW startup_py = {.cb = sizeof(startup_py)};
                PROCESS_INFORMATION process_py;
                if (CreateProcessW(NULL, line, NULL, NULL, FALSE, 0, NULL, dir, &startup_py, &process_py)) {
                    CloseHandle(process_py.hThread);
                    game = process_py.hProcess;
                }
            }
        }
    }
    if (!game) {
        /* "Not found" only when it really is missing: every failure used to say so. */
        if (GetFileAttributesW(exe) == INVALID_FILE_ATTRIBUTES) {
            swprintf(text, 1024, L"Bloodborne.exe or run.py was not found in:\n%ls", dir);
        } else {
            wchar_t reason[512] = L"";
            FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, error, 0,
                           reason, 512, NULL);
            swprintf(text, 1024, L"Could not start Bloodborne (error %lu): %ls", (unsigned long)error,
                     reason);
        }
        MessageBoxW(NULL, text, L"Bloodborne", MB_OK | MB_ICONERROR);
        return 1;
    }
    WaitForSingleObject(game, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(game, &code);
    CloseHandle(game);
    return (int)code;
}

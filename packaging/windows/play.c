/* Play Bloodborne.exe: starts the game with the settings saved by the launcher, without opening
 * the launcher (it runs `Bloodborne.exe --play` from its own folder and waits for it, so Steam
 * and other front ends see the game running). Extra arguments are passed on. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <wchar.h>

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
            static wchar_t cand[16][32768];
            int num_cands = 0;
            wchar_t appdata[32768] = L"";
            GetEnvironmentVariableW(L"LOCALAPPDATA", appdata, 32768);
            if (appdata[0]) {
                swprintf(cand[num_cands++], 32768, L"%ls\\Programs\\Python\\Python313\\python.exe", appdata);
                swprintf(cand[num_cands++], 32768, L"%ls\\Programs\\Python\\Python314\\python.exe", appdata);
                swprintf(cand[num_cands++], 32768, L"%ls\\Programs\\Python\\Python312\\python.exe", appdata);
                swprintf(cand[num_cands++], 32768, L"%ls\\Programs\\Python\\Python311\\python.exe", appdata);
                swprintf(cand[num_cands++], 32768, L"%ls\\Programs\\Python\\Python310\\python.exe", appdata);
                swprintf(cand[num_cands++], 32768, L"%ls\\Bloodborne\\runtime\\python.exe", appdata);
            }
            wcscpy(cand[num_cands++], L"python.exe");
            wcscpy(cand[num_cands++], L"pythonw.exe");
            swprintf(cand[num_cands++], 32768, L"%ls\\_internal\\python.exe", dir);

            for (int i = 0; i < num_cands; i++) {
                if (cand[i][1] == L':' && GetFileAttributesW(cand[i]) == INVALID_FILE_ATTRIBUTES)
                    continue;
                swprintf(parameters, 32768, L"\"%ls\"%ls%ls", py_script,
                         arguments && *arguments ? L" " : L"", arguments ? arguments : L"");
                swprintf(line, 32768, L"\"%ls\" %ls", cand[i], parameters);

                STARTUPINFOW startup_py = {.cb = sizeof(startup_py)};
                PROCESS_INFORMATION process_py;
                if (CreateProcessW(NULL, line, NULL, NULL, FALSE, 0, NULL, dir, &startup_py, &process_py)) {
                    CloseHandle(process_py.hThread);
                    game = process_py.hProcess;
                    break;
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

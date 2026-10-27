/* Bloodborne Launcher.exe: opens the Bloodborne graphical launcher / settings UI.
 * Passes --launcher so the settings GUI always opens directly in a single click.
 * Falls back to pythonw/python if Bloodborne.exe was moved or missing. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <wchar.h>
#include <shlwapi.h>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR arguments, int show)
{
    (void)instance; (void)previous; (void)show;
    static wchar_t dir[32768], exe[32768], parameters[32768], line[32768], text[1024];
    DWORD length = GetModuleFileNameW(NULL, dir, 32768);
    if (length == 0 || length >= 32768)
        return 1;
    wchar_t *slash = wcsrchr(dir, L'\\');
    if (slash)
        *slash = 0;

    swprintf(exe, 32768, L"%ls\\Bloodborne.exe", dir);
    if (GetFileAttributesW(exe) == INVALID_FILE_ATTRIBUTES) {
        static wchar_t sub_exe[32768];
        swprintf(sub_exe, 32768, L"%ls\\BloodBorne Port\\Bloodborne.exe", dir);
        if (GetFileAttributesW(sub_exe) != INVALID_FILE_ATTRIBUTES) {
            wcscpy(exe, sub_exe);
            swprintf(dir, 32768, L"%ls\\BloodBorne Port", dir);
        }
    }

    HANDLE proc = NULL;
    DWORD error = 0;

    /* Check if standalone Bloodborne.exe exists */
    if (GetFileAttributesW(exe) != INVALID_FILE_ATTRIBUTES) {
        swprintf(parameters, 32768, L"--launcher%ls%ls", arguments && *arguments ? L" " : L"",
                 arguments ? arguments : L"");
        swprintf(line, 32768, L"\"%ls\" %ls", exe, parameters);

        STARTUPINFOW startup = {.cb = sizeof(startup)};
        PROCESS_INFORMATION process;
        if (CreateProcessW(exe, line, NULL, NULL, FALSE, 0, NULL, dir, &startup, &process)) {
            CloseHandle(process.hThread);
            CloseHandle(process.hProcess);
            return 0;
        } else {
            error = GetLastError();
            if (error == ERROR_ELEVATION_REQUIRED) {
                SHELLEXECUTEINFOW shell = {
                    .cbSize = sizeof(shell),
                    .fMask = SEE_MASK_NOCLOSEPROCESS,
                    .lpFile = exe,
                    .lpParameters = parameters,
                    .lpDirectory = dir,
                    .nShow = SW_SHOWNORMAL
                };
                if (ShellExecuteExW(&shell)) {
                    if (shell.hProcess) CloseHandle(shell.hProcess);
                    return 0;
                }
            }
        }
    }

    /* Fallback: try pythonw or python with launcher/bbport_launcher_win.py */
    static wchar_t py_script[32768];
    swprintf(py_script, 32768, L"%ls\\launcher\\bbport_launcher_win.py", dir);
    if (GetFileAttributesW(py_script) != INVALID_FILE_ATTRIBUTES) {
        /* Check AppData Python, runtime, then PATH */
        static wchar_t cand[16][32768];
        int num_cands = 0;
        wchar_t appdata[32768] = L"";
        GetEnvironmentVariableW(L"LOCALAPPDATA", appdata, 32768);
        if (appdata[0]) {
            swprintf(cand[num_cands++], 32768, L"%ls\\Programs\\Python\\Python313\\pythonw.exe", appdata);
            swprintf(cand[num_cands++], 32768, L"%ls\\Programs\\Python\\Python314\\pythonw.exe", appdata);
            swprintf(cand[num_cands++], 32768, L"%ls\\Programs\\Python\\Python312\\pythonw.exe", appdata);
            swprintf(cand[num_cands++], 32768, L"%ls\\Programs\\Python\\Python311\\pythonw.exe", appdata);
            swprintf(cand[num_cands++], 32768, L"%ls\\Programs\\Python\\Python310\\pythonw.exe", appdata);
            swprintf(cand[num_cands++], 32768, L"%ls\\Bloodborne\\runtime\\pythonw.exe", appdata);
        }
        wcscpy(cand[num_cands++], L"pythonw.exe");
        wcscpy(cand[num_cands++], L"python.exe");
        swprintf(cand[num_cands++], 32768, L"%ls\\_internal\\pythonw.exe", dir);
        swprintf(cand[num_cands++], 32768, L"%ls\\_internal\\python.exe", dir);

        for (int i = 0; i < num_cands; i++) {
            if (cand[i][1] == L':' && GetFileAttributesW(cand[i]) == INVALID_FILE_ATTRIBUTES)
                continue;
            swprintf(parameters, 32768, L"\"%ls\" --launcher%ls%ls", py_script,
                     arguments && *arguments ? L" " : L"", arguments ? arguments : L"");
            swprintf(line, 32768, L"\"%ls\" %ls", cand[i], parameters);

            STARTUPINFOW startup = {.cb = sizeof(startup)};
            PROCESS_INFORMATION process;
            if (CreateProcessW(NULL, line, NULL, NULL, FALSE, 0, NULL, dir, &startup, &process)) {
                CloseHandle(process.hThread);
                CloseHandle(process.hProcess);
                return 0;
            }
        }
    }

    /* If neither worked, report error */
    swprintf(text, 1024,
             L"Could not launch Bloodborne Launcher.\n\n"
             L"Please make sure Bloodborne.exe and the _internal folder are present in:\n%ls", dir);
    MessageBoxW(NULL, text, L"Bloodborne Launcher", MB_OK | MB_ICONERROR);
    return 1;
}

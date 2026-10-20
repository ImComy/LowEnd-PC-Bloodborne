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
        /* "Not found" only when it really is missing: every failure used to say so. */
        if (GetFileAttributesW(exe) == INVALID_FILE_ATTRIBUTES) {
            swprintf(text, 1024, L"Bloodborne.exe was not found next to this file:\n%ls", dir);
        } else {
            wchar_t reason[512] = L"";
            FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, error, 0,
                           reason, 512, NULL);
            swprintf(text, 1024, L"Could not start Bloodborne.exe (error %lu): %ls", (unsigned long)error,
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

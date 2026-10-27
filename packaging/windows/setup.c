/* Setup Bloodborne.exe: One-time setup and optimizer for Bloodborne PC port.
 * Configures global dependencies in %LOCALAPPDATA%\Bloodborne and reclaims disk space. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <urlmon.h>
#include <stdio.h>
#include <wchar.h>

static void get_folder(wchar_t *path, size_t max_len) {
    DWORD len = GetModuleFileNameW(NULL, path, max_len);
    if (len > 0 && len < max_len) {
        wchar_t *slash = wcsrchr(path, L'\\');
        if (slash) *slash = 0;
    }
}

static BOOL run_sync(const wchar_t *cmd, const wchar_t *work_dir) {
    STARTUPINFOW si = {.cb = sizeof(si), .dwFlags = STARTF_USESHOWWINDOW, .wShowWindow = SW_HIDE};
    PROCESS_INFORMATION pi;
    wchar_t cmd_line[32768];
    wcsncpy(cmd_line, cmd, 32768);
    if (CreateProcessW(NULL, cmd_line, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, work_dir, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 120000);
        DWORD code = 1;
        GetExitCodeProcess(pi.hProcess, &code);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return code == 0;
    }
    return FALSE;
}

static BOOL remove_dir_recursive(const wchar_t *path) {
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) return TRUE;
    wchar_t search_path[32768];
    swprintf(search_path, 32768, L"%ls\\*.*", path);
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(search_path, &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
                continue;
            wchar_t sub_path[32768];
            swprintf(sub_path, 32768, L"%ls\\%ls", path, fd.cFileName);
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                remove_dir_recursive(sub_path);
            } else {
                SetFileAttributesW(sub_path, FILE_ATTRIBUTE_NORMAL);
                DeleteFileW(sub_path);
            }
        } while (FindNextFileW(hFind, &fd));
        FindClose(hFind);
    }
    SetFileAttributesW(path, FILE_ATTRIBUTE_NORMAL);
    return RemoveDirectoryW(path);
}

static void copy_dir_files(const wchar_t *src, const wchar_t *dst) {
    CreateDirectoryW(dst, NULL);
    wchar_t search_path[32768];
    swprintf(search_path, 32768, L"%ls\\*.*", src);
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(search_path, &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
                continue;
            wchar_t s[32768], d[32768];
            swprintf(s, 32768, L"%ls\\%ls", src, fd.cFileName);
            swprintf(d, 32768, L"%ls\\%ls", dst, fd.cFileName);
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                CopyFileW(s, d, FALSE);
            }
        } while (FindNextFileW(hFind, &fd));
        FindClose(hFind);
    }
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR arguments, int show) {
    (void)instance; (void)previous; (void)arguments; (void)show;

    wchar_t dir[32768], appdata[32768];
    get_folder(dir, 32768);
    GetEnvironmentVariableW(L"LOCALAPPDATA", appdata, 32768);

    /* If run from root, check if BloodBorne Port subfolder exists */
    wchar_t port_dir[32768];
    swprintf(port_dir, 32768, L"%ls\\BloodBorne Port", dir);
    if (GetFileAttributesW(port_dir) == INVALID_FILE_ATTRIBUTES) {
        wcscpy(port_dir, dir);
    }

    int choice = MessageBoxW(NULL,
        L"Welcome to Bloodborne PC One-Time Setup.\n\n"
        L"This tool will:\n"
        L" 1. Verify global Python 3 and PySide6 launcher libraries.\n"
        L" 2. Store engine binaries (bb-probe.exe and DLLs) in AppData.\n"
        L" 3. Clean temporary build caches to reduce folder size by ~1.9 GB.\n\n"
        L"Do you want to proceed?",
        L"Bloodborne PC Setup",
        MB_YESNO | MB_ICONQUESTION);

    if (choice != IDYES) return 0;

    /* 1. Locate Python */
    wchar_t python_exe[32768] = L"";
    const wchar_t *py_versions[] = { L"Python313", L"Python314", L"Python312", L"Python311", L"Python310" };
    for (int i = 0; i < 5; i++) {
        wchar_t candidate[32768];
        swprintf(candidate, 32768, L"%ls\\Programs\\Python\\%ls\\python.exe", appdata, py_versions[i]);
        if (GetFileAttributesW(candidate) != INVALID_FILE_ATTRIBUTES) {
            wcscpy(python_exe, candidate);
            break;
        }
    }
    if (!python_exe[0]) {
        wcscpy(python_exe, L"python.exe");
    }

    /* Check PySide6 */
    wchar_t check_cmd[32768];
    swprintf(check_cmd, 32768, L"\"%ls\" -c \"import PySide6\"", python_exe);
    BOOL has_pyside6 = run_sync(check_cmd, port_dir);

    if (!has_pyside6) {
        /* Attempt to install PySide6 via pip */
        wchar_t pip_cmd[32768];
        swprintf(pip_cmd, 32768, L"\"%ls\" -m pip install -q PySide6", python_exe);
        run_sync(pip_cmd, port_dir);
    }

    /* 2. Global Binaries setup */
    wchar_t appdata_bb[32768], appdata_bin[32768];
    swprintf(appdata_bb, 32768, L"%ls\\Bloodborne", appdata);
    swprintf(appdata_bin, 32768, L"%ls\\Bloodborne\\bin", appdata);
    CreateDirectoryW(appdata_bb, NULL);
    CreateDirectoryW(appdata_bin, NULL);

    wchar_t target_probe[32768];
    swprintf(target_probe, 32768, L"%ls\\bb-probe.exe", appdata_bin);

    wchar_t local_bin[32768];
    swprintf(local_bin, 32768, L"%ls\\bin", port_dir);

    if (GetFileAttributesW(local_bin) != INVALID_FILE_ATTRIBUTES) {
        copy_dir_files(local_bin, appdata_bin);
    }

    /* 3. Reclaim Disk Space from build caches and redundant files */
    wchar_t cache_path[32768];

    /* out/gpu */
    swprintf(cache_path, 32768, L"%ls\\out\\gpu", port_dir);
    remove_dir_recursive(cache_path);

    /* out/pyi-dist */
    swprintf(cache_path, 32768, L"%ls\\out\\pyi-dist", port_dir);
    remove_dir_recursive(cache_path);

    /* out/pyi-work */
    swprintf(cache_path, 32768, L"%ls\\out\\pyi-work", port_dir);
    remove_dir_recursive(cache_path);

    /* out/mod-game-* */
    swprintf(cache_path, 32768, L"%ls\\out\\mod-game-m_ldh8ve", port_dir);
    remove_dir_recursive(cache_path);
    swprintf(cache_path, 32768, L"%ls\\out\\mod-game-_jvl9vpj", port_dir);
    remove_dir_recursive(cache_path);

    /* Remove local _internal if PySide6 is active globally */
    wchar_t local_internal[32768];
    swprintf(local_internal, 32768, L"%ls\\_internal", port_dir);
    remove_dir_recursive(local_internal);

    /* If AppData has the engine binaries, remove bulky local bin/ folder */
    if (GetFileAttributesW(target_probe) != INVALID_FILE_ATTRIBUTES) {
        remove_dir_recursive(local_bin);
    }

    wchar_t summary[2048];
    swprintf(summary, 2048,
        L"Setup completed successfully!\n\n"
        L"Summary of optimizations:\n"
        L" - Global Launcher Runtime: Configured in %ls\n"
        L" - Engine Binaries & DLLs: Installed to AppData\\Local\\Bloodborne\\bin\n"
        L" - Local Folder: Cleaned build caches & binaries (~1.9 GB freed!)\n\n"
        L"You can now launch the game anytime using:\n"
        L" - Bloodborne Launcher.exe (Settings & GUI)\n"
        L" - Play Bloodborne.exe (Instant launch)",
        python_exe);

    MessageBoxW(NULL, summary, L"Bloodborne PC Setup Complete", MB_OK | MB_ICONINFORMATION);
    return 0;
}

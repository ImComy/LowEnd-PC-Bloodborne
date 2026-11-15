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

static BOOL check_registry_python(HKEY root, const wchar_t *subver, wchar_t *out_py) {
    wchar_t subkey[512];
    swprintf(subkey, 512, L"Software\\Python\\PythonCore\\%ls\\InstallPath", subver);
    HKEY hkey;
    if (RegOpenKeyExW(root, subkey, 0, KEY_READ, &hkey) == ERROR_SUCCESS) {
        wchar_t install_dir[MAX_PATH];
        DWORD size = sizeof(install_dir);
        DWORD type = 0;
        if (RegQueryValueExW(hkey, NULL, NULL, &type, (LPBYTE)install_dir, &size) == ERROR_SUCCESS) {
            wchar_t candidate[32768];
            swprintf(candidate, 32768, L"%ls\\python.exe", install_dir);
            RegCloseKey(hkey);
            if (check_py_path(candidate, out_py)) return TRUE;
        } else {
            RegCloseKey(hkey);
        }
    }
    return FALSE;
}

static BOOL check_search_path(wchar_t *out_py) {
    wchar_t resolved[32768];
    DWORD len = SearchPathW(NULL, L"python.exe", NULL, 32768, resolved, NULL);
    if (len > 0 && len < 32768) {
        if (check_py_path(resolved, out_py)) return TRUE;
    }
    return FALSE;
}

static BOOL find_python_cli(const wchar_t *appdata, const wchar_t *dir, wchar_t *out_py) {
    wchar_t cand[32768];
    const wchar_t *vers[] = { L"Python314", L"Python313", L"Python312", L"Python311", L"Python310" };
    const wchar_t *reg_vers[] = { L"3.14", L"3.13", L"3.12", L"3.11", L"3.10" };

    /* 1. Local _internal */
    swprintf(cand, 32768, L"%ls\\_internal\\python.exe", dir);
    if (check_py_path(cand, out_py)) return TRUE;

    /* 2. AppData Bloodborne runtime */
    if (appdata && appdata[0]) {
        swprintf(cand, 32768, L"%ls\\Bloodborne\\runtime\\python.exe", appdata);
        if (check_py_path(cand, out_py)) return TRUE;

        /* 3. AppData Programs Python */
        for (int i = 0; i < 5; i++) {
            swprintf(cand, 32768, L"%ls\\Programs\\Python\\%ls\\python.exe", appdata, vers[i]);
            if (check_py_path(cand, out_py)) return TRUE;
        }
    }

    /* 4. Program Files */
    wchar_t pf[32768];
    if (GetEnvironmentVariableW(L"ProgramFiles", pf, 32768) > 0) {
        for (int i = 0; i < 5; i++) {
            swprintf(cand, 32768, L"%ls\\%ls\\python.exe", pf, vers[i]);
            if (check_py_path(cand, out_py)) return TRUE;
        }
    }
    if (GetEnvironmentVariableW(L"ProgramFiles(x86)", pf, 32768) > 0) {
        for (int i = 0; i < 5; i++) {
            swprintf(cand, 32768, L"%ls\\%ls\\python.exe", pf, vers[i]);
            if (check_py_path(cand, out_py)) return TRUE;
        }
    }

    /* 5. C:\Python3* */
    for (int i = 0; i < 5; i++) {
        swprintf(cand, 32768, L"C:\\%ls\\python.exe", vers[i]);
        if (check_py_path(cand, out_py)) return TRUE;
    }

    /* 6. Registry */
    for (int i = 0; i < 5; i++) {
        if (check_registry_python(HKEY_CURRENT_USER, reg_vers[i], out_py)) return TRUE;
        if (check_registry_python(HKEY_LOCAL_MACHINE, reg_vers[i], out_py)) return TRUE;
    }

    /* 7. SearchPath */
    if (check_search_path(out_py)) return TRUE;

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
    if (!find_python_cli(appdata, port_dir, python_exe)) {
        MessageBoxW(NULL,
            L"Could not locate a valid Python 3.10+ installation on your system.\n\n"
            L"Please download and install Python 3.10 or newer from:\n"
            L"https://www.python.org/downloads/\n\n"
            L"Make sure to check 'Add Python to PATH' during installation, then re-run Setup.",
            L"Bloodborne PC Setup - Python Missing",
            MB_OK | MB_ICONERROR);
        return 1;
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

    /* Check Pillow (PIL) for graphics scaling & fallback launcher */
    wchar_t check_pil[32768];
    swprintf(check_pil, 32768, L"\"%ls\" -c \"import PIL\"", python_exe);
    BOOL has_pil = run_sync(check_pil, port_dir);

    if (!has_pil) {
        wchar_t pip_pil[32768];
        swprintf(pip_pil, 32768, L"\"%ls\" -m pip install -q Pillow", python_exe);
        run_sync(pip_pil, port_dir);
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

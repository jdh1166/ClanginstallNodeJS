#include <windows.h>
#include <shlobj.h>
#include <direct.h>
#include <io.h>
#include <stdio.h>

int copy_file(const char *src, const char *dst) {
    return CopyFileA(src, dst, FALSE) ? 0 : 1;
}

int copy_directory(const char *src, const char *dst) {
    _mkdir(dst);
    char search[MAX_PATH];
    snprintf(search, sizeof(search), "%s\\*", src);

    struct _finddata_t fd;
    intptr_t handle = _findfirst(search, &fd);
    if (handle == -1) return 1;

    do {
        if (strcmp(fd.name, ".") == 0 || strcmp(fd.name, "..") == 0) continue;

        char srcPath[MAX_PATH], dstPath[MAX_PATH];
        snprintf(srcPath, sizeof(srcPath), "%s\\%s", src, fd.name);
        snprintf(dstPath, sizeof(dstPath), "%s\\%s", dst, fd.name);

        if (fd.attrib & _A_SUBDIR) {
            copy_directory(srcPath, dstPath);
        } else {
            copy_file(srcPath, dstPath);
        }
    } while (_findnext(handle, &fd) == 0);

    _findclose(handle);
    return 0;
}

int update_path_env(const char *addPath) {
    HKEY hKey;
    char currentPath[4096];
    DWORD size = sizeof(currentPath);

    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
        "SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment",
        0, KEY_READ | KEY_WRITE, &hKey) != ERROR_SUCCESS) {
        MessageBoxW(NULL, L"레지스트리 열기 실패", L"에러", MB_ICONERROR);
        return 1;
    }

    RegQueryValueExA(hKey, "Path", NULL, NULL, (LPBYTE)currentPath, &size);

    if (strstr(currentPath, addPath)) {
        RegCloseKey(hKey);
        return 0;
    }

    strcat(currentPath, ";");
    strcat(currentPath, addPath);

    if (RegSetValueExA(hKey, "Path", 0, REG_EXPAND_SZ, (LPBYTE)currentPath, (DWORD)strlen(currentPath) + 1) != ERROR_SUCCESS) {
        RegCloseKey(hKey);
        MessageBoxW(NULL, L"환경변수 등록 실패", L"에러", MB_ICONERROR);
        return 1;
    }

    RegCloseKey(hKey);
    return 0;
}

void notify_environment_change() {
    SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0,
        (LPARAM)L"Environment", SMTO_ABORTIFHUNG, 5000, NULL);
}

int main() {
    char programFilesPath[MAX_PATH];
    SHGetFolderPathA(NULL, CSIDL_PROGRAM_FILES, NULL, 0, programFilesPath);

    char destPath[MAX_PATH];
    snprintf(destPath, sizeof(destPath), "%s\\NodeJS", programFilesPath);

    if (copy_directory("nodejs", destPath) != 0) {
        MessageBoxW(NULL, L"nodejs 폴더 복사에 실패했습니다.", L"에러", MB_ICONERROR);
        return 1;
    }

    if (update_path_env(destPath) != 0) {
        // 에러 메시지는 update_path_env 안에서 띄워줌
        return 1;
    }

    notify_environment_change();

    MessageBoxW(NULL, L"Node.js 실행 환경이 성공적으로 구성되었습니다!", L"설치 완료", MB_OK);
    return 0;
}

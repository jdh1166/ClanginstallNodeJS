#include <windows.h>
#include <shlobj.h>
#include <direct.h>
#include <io.h>
#include <stdio.h>
#include <string.h>

int copy_file(const char *src, const char *dst) {
    if (!CopyFileA(src, dst, FALSE)) {
        wchar_t msg[512];
        swprintf(msg, 512, L"파일 복사 실패: %S -> %S", src, dst);
        MessageBoxW(NULL, msg, L"에러", MB_ICONERROR);
        return 1;
    }
    return 0;
}

int copy_directory(const char *src, const char *dst) {
    if (_mkdir(dst) != 0 && errno != EEXIST) {
        wchar_t msg[512];
        swprintf(msg, 512, L"폴더 생성 실패: %S", dst);
        MessageBoxW(NULL, msg, L"에러", MB_ICONERROR);
        return 1;
    }

    char search[MAX_PATH];
    int ret = snprintf(search, sizeof(search), "%s\\*", src);
    if (ret < 0 || ret >= sizeof(search)) {
        MessageBoxW(NULL, L"경로 문자열 생성 실패", L"에러", MB_ICONERROR);
        return 1;
    }

    struct _finddata_t fd;
    intptr_t handle = _findfirst(search, &fd);
    if (handle == -1) {
        wchar_t msg[512];
        swprintf(msg, 512, L"디렉토리 검색 실패: %S", src);
        MessageBoxW(NULL, msg, L"에러", MB_ICONERROR);
        return 1;
    }

    do {
        if (strcmp(fd.name, ".") == 0 || strcmp(fd.name, "..") == 0)
            continue;

        char srcPath[MAX_PATH], dstPath[MAX_PATH];
        ret = snprintf(srcPath, sizeof(srcPath), "%s\\%s", src, fd.name);
        if (ret < 0 || ret >= sizeof(srcPath)) {
            MessageBoxW(NULL, L"srcPath 문자열 생성 실패", L"에러", MB_ICONERROR);
            _findclose(handle);
            return 1;
        }
        ret = snprintf(dstPath, sizeof(dstPath), "%s\\%s", dst, fd.name);
        if (ret < 0 || ret >= sizeof(dstPath)) {
            MessageBoxW(NULL, L"dstPath 문자열 생성 실패", L"에러", MB_ICONERROR);
            _findclose(handle);
            return 1;
        }

        if (fd.attrib & _A_SUBDIR) {
            if (copy_directory(srcPath, dstPath) != 0) {
                _findclose(handle);
                return 1;
            }
        } else {
            if (copy_file(srcPath, dstPath) != 0) {
                _findclose(handle);
                return 1;
            }
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

    if (RegQueryValueExA(hKey, "Path", NULL, NULL, (LPBYTE)currentPath, &size) != ERROR_SUCCESS) {
        RegCloseKey(hKey);
        MessageBoxW(NULL, L"환경변수 읽기 실패", L"에러", MB_ICONERROR);
        return 1;
    }

    if (strstr(currentPath, addPath)) {
        RegCloseKey(hKey);
        return 0;
    }

    if (strlen(currentPath) + strlen(addPath) + 2 > sizeof(currentPath)) {
        RegCloseKey(hKey);
        MessageBoxW(NULL, L"환경변수 값이 너무 김", L"에러", MB_ICONERROR);
        return 1;
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
    if (SHGetFolderPathA(NULL, CSIDL_PROGRAM_FILES, NULL, 0, programFilesPath) != S_OK) {
        MessageBoxW(NULL, L"Program Files 경로 조회 실패", L"에러", MB_ICONERROR);
        return 1;
    }

    char destPath[MAX_PATH];
    int ret = snprintf(destPath, sizeof(destPath), "%s\\NodeJS", programFilesPath);
    if (ret < 0 || ret >= sizeof(destPath)) {
        MessageBoxW(NULL, L"목적지 경로 생성 실패", L"에러", MB_ICONERROR);
        return 1;
    }

    if (copy_directory("nodejs", destPath) != 0) {
        // 에러 메시지는 copy_directory 내부에서 출력됨
        return 1;
    }

    if (update_path_env(destPath) != 0) {
        // 에러 메시지는 update_path_env 내부에서 출력됨
        return 1;
    }

    notify_environment_change();

    MessageBoxW(NULL, L"Node.js 실행 환경이 성공적으로 구성되었습니다!", L"설치 완료", MB_OK);
    return 0;
}

#include <windows.h>
#include <shlobj.h>
#include <direct.h>
#include <io.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

int copy_file(const char *src, const char *dst) {
    printf("[파일 복사] %s -> %s\n", src, dst);
    if (!CopyFileA(src, dst, FALSE)) {
        printf("[에러] 파일 복사 실패: %s -> %s\n", src, dst);
        return 1;
    }
    return 0;
}

int copy_directory(const char *src, const char *dst) {
    printf("[폴더 생성] %s\n", dst);
    if (_mkdir(dst) != 0 && errno != EEXIST) {
        printf("[에러] 폴더 생성 실패: %s\n", dst);
        return 1;
    }

    char search[MAX_PATH];
    int ret = snprintf(search, sizeof(search), "%s\\*", src);
    if (ret < 0 || ret >= (int)sizeof(search)) {
        printf("[에러] 경로 문자열 생성 실패\n");
        return 1;
    }

    struct _finddata_t fd;
    intptr_t handle = _findfirst(search, &fd);
    if (handle == -1) {
        printf("[에러] 디렉토리 검색 실패: %s\n", src);
        return 1;
    }

    do {
        if (strcmp(fd.name, ".") == 0 || strcmp(fd.name, "..") == 0)
            continue;

        char srcPath[MAX_PATH], dstPath[MAX_PATH];
        ret = snprintf(srcPath, sizeof(srcPath), "%s\\%s", src, fd.name);
        if (ret < 0 || ret >= (int)sizeof(srcPath)) {
            printf("[에러] srcPath 문자열 생성 실패\n");
            _findclose(handle);
            return 1;
        }
        ret = snprintf(dstPath, sizeof(dstPath), "%s\\%s", dst, fd.name);
        if (ret < 0 || ret >= (int)sizeof(dstPath)) {
            printf("[에러] dstPath 문자열 생성 실패\n");
            _findclose(handle);
            return 1;
        }

        if (fd.attrib & _A_SUBDIR) {
            printf("[디렉토리 복사] %s -> %s\n", srcPath, dstPath);
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
    printf("[환경 변수 업데이트] %s\n", addPath);
    HKEY hKey;
    char currentPath[4096];
    DWORD size = sizeof(currentPath);

    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
        "SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment",
        0, KEY_READ | KEY_WRITE, &hKey) != ERROR_SUCCESS) {
        printf("[에러] 레지스트리 열기 실패\n");
        return 1;
    }

    if (RegQueryValueExA(hKey, "Path", NULL, NULL, (LPBYTE)currentPath, &size) != ERROR_SUCCESS) {
        RegCloseKey(hKey);
        printf("[에러] 환경변수 읽기 실패\n");
        return 1;
    }

    if (strstr(currentPath, addPath)) {
        printf("[정보] 이미 환경변수에 경로가 포함되어 있습니다.\n");
        RegCloseKey(hKey);
        return 0;
    }

    if (strlen(currentPath) + strlen(addPath) + 2 > sizeof(currentPath)) {
        RegCloseKey(hKey);
        printf("[에러] 환경변수 값이 너무 깁니다.\n");
        return 1;
    }

    strcat(currentPath, ";");
    strcat(currentPath, addPath);

    if (RegSetValueExA(hKey, "Path", 0, REG_EXPAND_SZ, (LPBYTE)currentPath, (DWORD)strlen(currentPath) + 1) != ERROR_SUCCESS) {
        RegCloseKey(hKey);
        printf("[에러] 환경변수 등록 실패\n");
        return 1;
    }

    RegCloseKey(hKey);
    printf("[성공] 환경변수 업데이트 완료\n");
    return 0;
}

void notify_environment_change() {
    printf("[알림] 환경변수 변경 알림 보내는 중...\n");
    SendMessageTimeoutA(HWND_BROADCAST, WM_SETTINGCHANGE, 0,
        (LPARAM)"Environment", SMTO_ABORTIFHUNG, 5000, NULL);
    printf("[알림] 완료\n");
}

BOOL IsRunningAsAdmin() {
    BOOL isAdmin = FALSE;
    PSID adminGroup = NULL;
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&ntAuthority, 2,
        SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS,
        0, 0, 0, 0, 0, 0, &adminGroup)) {
        CheckTokenMembership(NULL, adminGroup, &isAdmin);
        FreeSid(adminGroup);
    }
    return isAdmin;
}

int RelaunchAsAdmin() {
    WCHAR szPath[MAX_PATH];
    if (!GetModuleFileNameW(NULL, szPath, MAX_PATH)) {
        MessageBoxW(NULL, L"실행 경로를 찾을 수 없습니다.", L"에러", MB_OK | MB_ICONERROR);
        return 1;
    }

    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    sei.lpVerb = L"runas";
    sei.lpFile = szPath;
    sei.hwnd = NULL;
    sei.nShow = SW_NORMAL;

    if (!ShellExecuteExW(&sei)) {
        DWORD err = GetLastError();
        if (err == ERROR_CANCELLED) {
            MessageBoxW(NULL, L"너무해 ㅠ", L"권한 필요", MB_OK | MB_ICONWARNING);
        } else {
            MessageBoxW(NULL, L"관리자 권한으로 재실행 실패", L"에러", MB_OK | MB_ICONERROR);
        }
        return 1;
    }
    return 0;
}

int main() {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    if (!IsRunningAsAdmin()) {
        MessageBoxW(NULL, L"관리자 권한으로 실행해주세요", L"권한 필요", MB_OK | MB_ICONWARNING);
        if (RelaunchAsAdmin() != 0) {
            return 1;
        }
        return 0; // 관리자 권한 재실행 후 현재 프로세스는 종료
    }

    printf("=== Node.js 환경 설치 시작 ===\n");

    char programFilesPath[MAX_PATH];
    if (SHGetFolderPathA(NULL, CSIDL_PROGRAM_FILES, NULL, 0, programFilesPath) != S_OK) {
        printf("[에러] Program Files 경로 조회 실패\n");
        return 1;
    }

    char destPath[MAX_PATH];
    int ret = snprintf(destPath, sizeof(destPath), "%s\\NodeJS", programFilesPath);
    if (ret < 0 || ret >= (int)sizeof(destPath)) {
        printf("[에러] 목적지 경로 생성 실패\n");
        return 1;
    }

    if (copy_directory("nodejs", destPath) != 0) {
        printf("[에러] nodejs 폴더 복사 실패\n");
        return 1;
    }

    if (update_path_env(destPath) != 0) {
        printf("[에러] 환경변수 설정 실패\n");
        return 1;
    }

    notify_environment_change();

    printf("=== 설치 완료 ===\n");
    printf("Node.js 실행 환경이 성공적으로 구성되었습니다!\n");

    MessageBoxW(NULL, L"설치 완료", L"알림", MB_OK | MB_ICONINFORMATION);

    return 0;
}

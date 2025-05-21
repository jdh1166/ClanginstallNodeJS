#include <windows.h>
#include <shlobj.h>
#include <direct.h>
#include <io.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

BOOL DeleteDirectory(const char *szPath) {
    char szFind[MAX_PATH];
    WIN32_FIND_DATAA fdFile;
    HANDLE hFind = NULL;
    BOOL bRet = TRUE;

    snprintf(szFind, sizeof(szFind), "%s\\*", szPath);
    hFind = FindFirstFileA(szFind, &fdFile);

    if (hFind == INVALID_HANDLE_VALUE) {
        if (GetLastError() == ERROR_FILE_NOT_FOUND) {
            return RemoveDirectoryA(szPath);
        }
        return FALSE;
    }

    do {
        if (strcmp(fdFile.cFileName, ".") == 0 || strcmp(fdFile.cFileName, "..") == 0)
            continue;

        char szFile[MAX_PATH];
        snprintf(szFile, sizeof(szFile), "%s\\%s", szPath, fdFile.cFileName);

        if (fdFile.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (!DeleteDirectory(szFile)) {
                bRet = FALSE;
                break;
            }
        } else {
            SetFileAttributesA(szFile, FILE_ATTRIBUTE_NORMAL);
            if (!DeleteFileA(szFile)) {
                bRet = FALSE;
                break;
            }
        }
    } while (FindNextFileA(hFind, &fdFile));

    FindClose(hFind);

    if (bRet)
        bRet = RemoveDirectoryA(szPath);

    return bRet;
}

int update_path_remove(const char *removePath) {
    HKEY hKey;
    char currentPath[4096];
    DWORD size = sizeof(currentPath);

    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
        "SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment",
        0, KEY_READ | KEY_WRITE, &hKey) != ERROR_SUCCESS) {
        MessageBoxA(NULL, "레지스트리 열기 실패", "에러", MB_OK | MB_ICONERROR);
        return 1;
    }

    if (RegQueryValueExA(hKey, "Path", NULL, NULL, (LPBYTE)currentPath, &size) != ERROR_SUCCESS) {
        RegCloseKey(hKey);
        MessageBoxA(NULL, "환경변수 읽기 실패", "에러", MB_OK | MB_ICONERROR);
        return 1;
    }

    char *pos = strstr(currentPath, removePath);
    if (!pos) {
        RegCloseKey(hKey);
        MessageBoxA(NULL, "환경변수에 경로가 없습니다.", "정보", MB_OK | MB_ICONINFORMATION);
        return 0;
    }

    char newPath[4096] = {0};
    size_t removeLen = strlen(removePath);
    // 경로 앞에 세미콜론 포함 여부 체크 후 제거
    if (pos != currentPath && pos[-1] == ';') {
        // 앞에 ; 가 있을 경우 앞부분부터 pos-1까지 복사
        strncpy(newPath, currentPath, pos - currentPath - 1);
        newPath[pos - currentPath - 1] = '\0';
        strcat(newPath, pos + removeLen);
    } else if (pos == currentPath && pos[removeLen] == ';') {
        // 맨 앞이고 뒤에 ; 가 있을 경우 pos+removeLen+1부터 복사
        strcpy(newPath, pos + removeLen + 1);
    } else {
        // 경로만 제거 (양쪽에 ; 없을 경우)
        strncpy(newPath, currentPath, pos - currentPath);
        newPath[pos - currentPath] = '\0';
        strcat(newPath, pos + removeLen);
    }

    // 여백 생기면 세미콜론 중복 제거
    // 필요시 추가 정리 가능

    if (RegSetValueExA(hKey, "Path", 0, REG_EXPAND_SZ, (LPBYTE)newPath, (DWORD)strlen(newPath) + 1) != ERROR_SUCCESS) {
        RegCloseKey(hKey);
        MessageBoxA(NULL, "환경변수 수정 실패", "에러", MB_OK | MB_ICONERROR);
        return 1;
    }

    RegCloseKey(hKey);
    return 0;
}

void notify_environment_change() {
    SendMessageTimeoutA(HWND_BROADCAST, WM_SETTINGCHANGE, 0,
        (LPARAM)"Environment", SMTO_ABORTIFHUNG, 5000, NULL);
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
        return 0;
    }

    char programFilesPath[MAX_PATH];
    if (SHGetFolderPathA(NULL, CSIDL_PROGRAM_FILES, NULL, 0, programFilesPath) != S_OK) {
        MessageBoxA(NULL, "Program Files 경로 조회 실패", "에러", MB_OK | MB_ICONERROR);
        return 1;
    }

    char targetPath[MAX_PATH];
    snprintf(targetPath, sizeof(targetPath), "%s\\NodeJS", programFilesPath);

    if (!DeleteDirectory(targetPath)) {
        MessageBoxA(NULL, "NodeJS 폴더 삭제 실패", "에러", MB_OK | MB_ICONERROR);
        return 1;
    }

    if (update_path_remove(targetPath) != 0) {
        return 1;
    }

    notify_environment_change();

    MessageBoxW(NULL, L"제거 완료", L"알림", MB_OK | MB_ICONINFORMATION);

    return 0;
}

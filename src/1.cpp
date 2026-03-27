#pragma execution_character_set("utf-8")

#include <windows.h>
#include <shobjidl.h>
#include <shlobj.h>
#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <shlwapi.h>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "uuid.lib")

#ifndef INITGUID
#define INITGUID
#endif
#include <initguid.h>

DEFINE_GUID(IID_IShellLinkW, 0x000214F9, 0x0000, 0x0000, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
DEFINE_GUID(IID_IPersistFile, 0x0000010b, 0x0000, 0x0000, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);

std::wstring GetLongPath(const std::wstring& shortPath) {
    WCHAR longPath[MAX_PATH] = {0};
    DWORD length = GetLongPathNameW(shortPath.c_str(), longPath, MAX_PATH);
    if (length > 0) {
        return std::wstring(longPath);
    }
    return shortPath;
}

bool ResolveShortcut(LPCWSTR lpszShortcutPath, LPWSTR lpszTargetPath, DWORD cchTargetPath) {
    HRESULT hr;
    IShellLinkW* pShellLink = NULL;
    IPersistFile* pPersistFile = NULL;
    bool ifsuc = false;

    hr = CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (LPVOID*)&pShellLink);
    if (SUCCEEDED(hr)) {
        hr = pShellLink->QueryInterface(IID_IPersistFile, (LPVOID*)&pPersistFile);
        if (SUCCEEDED(hr)) {
            hr = pPersistFile->Load(lpszShortcutPath, STGM_READ);
            if (SUCCEEDED(hr)) {
                hr = pShellLink->GetPath(lpszTargetPath, cchTargetPath, NULL, SLGP_SHORTPATH);
                if (SUCCEEDED(hr)) {
                    ifsuc = true;
                }
            }
            pPersistFile->Release();
        }
        pShellLink->Release();
    }
    return ifsuc;
}

void FindGenshinInDirectory(const std::wstring& directory, std::vector<std::wstring>& foundPaths) {
    WIN32_FIND_DATAW findFileData;
    std::wstring searchPath = directory + L"\\*";

    HANDLE hFind = FindFirstFileW(searchPath.c_str(), &findFileData);
    if (hFind == INVALID_HANDLE_VALUE) {
        return;
    }

    do {
        std::wstring fileName = findFileData.cFileName;
        if (fileName == L"." || fileName == L"..") {
            continue;
        }

        std::wstring fullPath = directory + L"\\" + fileName;

        if (findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            FindGenshinInDirectory(fullPath, foundPaths);
        } 
        else {
            if (_wcsicmp(fileName.c_str(), L"YuanShen.exe") == 0 || 
                _wcsicmp(fileName.c_str(), L"GenshinImpact.exe") == 0) {
                foundPaths.push_back(GetLongPath(fullPath));
            }
        }
    } while (FindNextFileW(hFind, &findFileData) != 0);

    FindClose(hFind);
}

bool MatchTargetName(const std::wstring& fileName, const std::vector<std::wstring>& targetNames) {
    for (const auto& target : targetNames) {
        if (StrStrIW(fileName.c_str(), target.c_str()) != NULL) 
        {
            return true;
        }
    }
    return false;
}

void SearchShortcutsInDirectory(const std::wstring& directory, const std::vector<std::wstring>& targetNames, std::vector<std::wstring>& foundPaths) {
    WIN32_FIND_DATAW findFileData;
    std::wstring searchPath = directory + L"\\*";

    HANDLE hFind = FindFirstFileW(searchPath.c_str(), &findFileData);
    if (hFind == INVALID_HANDLE_VALUE) 
    {
        return;
    }

    do {
        std::wstring fileName = findFileData.cFileName;
        if (fileName == L"." || fileName == L"..") {
            continue;
        }

        std::wstring fullPath = directory + L"\\" + fileName;

        if (findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            SearchShortcutsInDirectory(fullPath, targetNames, foundPaths);
        } 
        else {
            if (fileName.length() >= 4 && _wcsicmp(fileName.substr(fileName.length() - 4).c_str(), L".lnk") == 0) {
                if (MatchTargetName(fileName, targetNames)) {
                    WCHAR szTargetPath[MAX_PATH] = {0};
                    if (ResolveShortcut(fullPath.c_str(), szTargetPath, MAX_PATH)) {
                        std::wstring targetPath = szTargetPath;
                        std::wstring targetFileName = PathFindFileNameW(targetPath.c_str());
                        
                        if (_wcsicmp(targetFileName.c_str(), L"YuanShen.exe") == 0 || 
                            _wcsicmp(targetFileName.c_str(), L"GenshinImpact.exe") == 0) {
                            foundPaths.push_back(GetLongPath(targetPath));
                        } 
                        else {
                            std::wstring targetDir = targetPath;
                            size_t pos = targetDir.find_last_of(L"\\/");
                            if (pos != std::wstring::npos) {
                                targetDir = targetDir.substr(0, pos);
                                FindGenshinInDirectory(GetLongPath(targetDir), foundPaths);
                            }
                        }
                    }
                }
            }
        }
    } while (FindNextFileW(hFind, &findFileData) != 0);

    FindClose(hFind);
}

std::wstring GetFolderPath(int csidl) {
    WCHAR szPath[MAX_PATH] = {0};
    if (SUCCEEDED(SHGetFolderPathW(NULL, csidl | CSIDL_FLAG_CREATE, NULL, 0, szPath))) {
        return std::wstring(szPath);
    }
    return L"";
}

int main() {
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) {
        std::cerr << "COM Init Failed." << std::endl;
        return 1;
    }

    std::vector<std::wstring> targetNames = {
        L"原神",
        L"Genshin Impact",
        L"米哈游启动器",
        L"HoYoverse Launcher"
    };

    std::vector<std::wstring> foundPaths;
    std::vector<std::wstring> searchDirs;
    
    searchDirs.push_back(GetFolderPath(CSIDL_DESKTOPDIRECTORY));
    searchDirs.push_back(GetFolderPath(CSIDL_COMMON_DESKTOPDIRECTORY));
    searchDirs.push_back(GetFolderPath(CSIDL_STARTMENU));
    searchDirs.push_back(GetFolderPath(CSIDL_COMMON_STARTMENU));

    for (const auto& dir : searchDirs) {
        if (!dir.empty()) {
            SearchShortcutsInDirectory(dir, targetNames, foundPaths);
        }
    }

    std::sort(foundPaths.begin(), foundPaths.end());
    auto last = std::unique(foundPaths.begin(), foundPaths.end());
    foundPaths.erase(last, foundPaths.end());

    if (foundPaths.empty()) {
        std::cout << "Not found." << std::endl;
    } 
    else {
        std::cout << "Found:" << std::endl;
        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        for (const auto& path : foundPaths) {
            WriteConsoleW(hConsole, path.c_str(), (DWORD)path.size(), NULL, NULL);
            WriteConsoleW(hConsole, L"\n", 1, NULL, NULL);
        }
    }

    CoUninitialize();
    return 0;
}


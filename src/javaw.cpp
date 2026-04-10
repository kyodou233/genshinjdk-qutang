#pragma comment(linker, "/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup")

#include <jni.h>
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

#define MAX_PATH_LEN 4096
#define MAX_ARGS 256

typedef jint (JNICALL* CreateJavaVM_t)(JavaVM**, void**, void*);


const char* classpath = ".";
const char* mainClassName = NULL;
int showVersion = 0;
int showHelp = 0;
int jvmOptionCount = 0;
int triggerGenshin = 0;
JavaVMOption jvmOptions[MAX_ARGS];

// ==========================================
// 第一部分：原神搜索逻辑
// ==========================================

std::wstring GetLongPath(const std::wstring& shortPath) {
    WCHAR longPath[MAX_PATH] = { 0 };
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
    bool success = false;

    hr = CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (LPVOID*)&pShellLink);
    if (SUCCEEDED(hr)) {
        hr = pShellLink->QueryInterface(IID_IPersistFile, (LPVOID*)&pPersistFile);
        if (SUCCEEDED(hr)) {
            hr = pPersistFile->Load(lpszShortcutPath, STGM_READ);
            if (SUCCEEDED(hr)) {
                hr = pShellLink->GetPath(lpszTargetPath, cchTargetPath, NULL, SLGP_SHORTPATH);
                if (SUCCEEDED(hr)) {
                    success = true;
                }
            }
            pPersistFile->Release();
        }
        pShellLink->Release();
    }
    return success;
}

void FindGenshinInDirectory(const std::wstring& directory, std::vector<std::wstring>& foundPaths) {
    WIN32_FIND_DATAW findFileData;
    std::wstring searchPath = directory + L"\\*";

    HANDLE hFind = FindFirstFileW(searchPath.c_str(), &findFileData);
    if (hFind == INVALID_HANDLE_VALUE) return;

    do {
        std::wstring fileName = findFileData.cFileName;
        if (fileName == L"." || fileName == L"..") continue;

        std::wstring fullPath = directory + L"\\" + fileName;

        if (findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            FindGenshinInDirectory(fullPath, foundPaths);
        } else {
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
        if (StrStrIW(fileName.c_str(), target.c_str()) != NULL) return true;
    }
    return false;
}

void SearchShortcutsInDirectory(const std::wstring& directory, const std::vector<std::wstring>& targetNames, std::vector<std::wstring>& foundPaths) {
    WIN32_FIND_DATAW findFileData;
    std::wstring searchPath = directory + L"\\*";

    HANDLE hFind = FindFirstFileW(searchPath.c_str(), &findFileData);
    if (hFind == INVALID_HANDLE_VALUE) return;

    do {
        std::wstring fileName = findFileData.cFileName;
        if (fileName == L"." || fileName == L"..") continue;

        std::wstring fullPath = directory + L"\\" + fileName;

        if (findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            SearchShortcutsInDirectory(fullPath, targetNames, foundPaths);
        } else {
            if (fileName.length() >= 4 && _wcsicmp(fileName.substr(fileName.length() - 4).c_str(), L".lnk") == 0) {
                if (MatchTargetName(fileName, targetNames)) {
                    WCHAR szTargetPath[MAX_PATH] = { 0 };
                    if (ResolveShortcut(fullPath.c_str(), szTargetPath, MAX_PATH)) {
                        std::wstring targetPath = szTargetPath;
                        std::wstring targetFileName = PathFindFileNameW(targetPath.c_str());

                        if (_wcsicmp(targetFileName.c_str(), L"YuanShen.exe") == 0 ||
                            _wcsicmp(targetFileName.c_str(), L"GenshinImpact.exe") == 0) {
                            foundPaths.push_back(GetLongPath(targetPath));
                        } else {
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
    WCHAR szPath[MAX_PATH] = { 0 };
    if (SUCCEEDED(SHGetFolderPathW(NULL, csidl | CSIDL_FLAG_CREATE, NULL, 0, szPath))) {
        return std::wstring(szPath);
    }
    return L"";
}

std::wstring FindGenshinPath() {
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

    if (foundPaths.size() > 0) {
        return foundPaths[0];
    }
    return L"";
}

// ==========================================
// 第二部分：启动原神
// ==========================================

void launch_genshin() {
    std::wstring genshin_path = FindGenshinPath();

    if (genshin_path.empty()) return;

    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    std::vector<WCHAR> cmd_line(genshin_path.size() + 10);
    wcscpy_s(cmd_line.data(), genshin_path.size() + 10, genshin_path.c_str());

    if (CreateProcessW(NULL, cmd_line.data(), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        ExitProcess(0);
    }
}

// ==========================================
// 第三部分：Java 逻辑
// ==========================================

void print_version() {
    printf("openjdk version \"17.0.喵\" 2026-03-24\n");
    printf("OpenJDK Runtime Environment (build 17.0.10+7)\n");
    printf("OpenJDK 64-Bit Server VM (build 17.0.喵+7, mixed mode, sharing)\n");
}

void print_help() {
    printf("Usage: java [-options] class\n");
}

int file_exists(const char* path) {
    DWORD attribs = GetFileAttributesA(path);
    return (attribs != INVALID_FILE_ATTRIBUTES && !(attribs & FILE_ATTRIBUTE_DIRECTORY));
}

int find_jvm_dll(char* buffer, int buffer_size) {
    char temp_path[MAX_PATH_LEN];
    char* java_home = getenv("JAVA_HOME");
    if (java_home) {
        snprintf(temp_path, MAX_PATH_LEN, "%s\\bin\\server\\jvm.dll", java_home);
        if (file_exists(temp_path)) { strncpy_s(buffer, buffer_size, temp_path, buffer_size - 1); return 1; }
    }
    
    const char* common_paths[] = {
        "C:\\Program Files\\Java\\jdk-17\\bin\\server\\jvm.dll",
        "D:\\lrz\\code\\jdK\\amazon-corretto-17.0.3.6.1-windows-x64-jdk\\jdk17.0.3_6\\bin\\server\\jvm.dll",
        NULL
    };
    for (int i = 0; common_paths[i] != NULL; i++) {
        if (file_exists(common_paths[i])) { strncpy_s(buffer, buffer_size, common_paths[i], buffer_size - 1); return 1; }
    }
    return 0;
}

int parse_args(int argc, char** argv) {
    int i = 1;
    while (i < argc) {
        if (strstr(argv[i], "--userType") != NULL ||
            strstr(argv[i], "--uuid") != NULL ||
            strstr(argv[i], "minecraft.launcher.brand") != NULL) {
            triggerGenshin = 1;
        }

        if (strcmp(argv[i], "-cp") == 0 || strcmp(argv[i], "-classpath") == 0) {
            if (i + 1 >= argc) return 0;
            classpath = argv[++i];
        } else if (strcmp(argv[i], "-version") == 0) {
            showVersion = 1;
        } else if (strcmp(argv[i], "-help") == 0) {
            showHelp = 1;
        } else if (strncmp(argv[i], "-D", 2) == 0 || strncmp(argv[i], "-X", 2) == 0) {
            jvmOptions[jvmOptionCount++].optionString = argv[i];
        } else {
            mainClassName = argv[i];
            break;
        }
        i++;
    }
    return 1;
}

// ==========================================
// Main
// ==========================================

int main(int argc, char** argv) {
    HRESULT hr_com = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    
    if (!parse_args(argc, argv)) return 1;

    if (triggerGenshin) {
        launch_genshin();
        return 0;
    }

    if (showHelp) { print_help(); return 0; }
    if (showVersion) { print_version(); return 0; }
    if (!mainClassName) { print_help(); return 0; }

    char jvm_path[MAX_PATH_LEN];
    if (!find_jvm_dll(jvm_path, MAX_PATH_LEN)) {
        fprintf(stderr, "Error: Cannot find jvm.dll\n");
        if (SUCCEEDED(hr_com)) CoUninitialize();
        return 1;
    }

    HINSTANCE jvmDll = LoadLibraryA(jvm_path);
    if (!jvmDll) return 1;

    CreateJavaVM_t createJavaVM = (CreateJavaVM_t)GetProcAddress(jvmDll, "JNI_CreateJavaVM");
    if (!createJavaVM) { FreeLibrary(jvmDll); return 1; }

    JavaVM* jvm;
    JNIEnv* env;
    JavaVMInitArgs vm_args;
    
    char cp_opt[MAX_PATH_LEN + 32];
    snprintf(cp_opt, sizeof(cp_opt), "-Djava.class.path=%s", classpath);
    
    JavaVMOption* options = new JavaVMOption[jvmOptionCount + 1];
    options[0].optionString = _strdup(cp_opt);
    for (int i = 0; i < jvmOptionCount; i++) {
        options[i + 1].optionString = jvmOptions[i].optionString;
    }

    memset(&vm_args, 0, sizeof(vm_args));
    vm_args.version = JNI_VERSION_1_8;
    vm_args.nOptions = jvmOptionCount + 1;
    vm_args.options = options;
    vm_args.ignoreUnrecognized = JNI_TRUE;

    jint res = createJavaVM(&jvm, (void**)&env, &vm_args);
    free(options[0].optionString);
    delete[] options;

    if (res != JNI_OK) { FreeLibrary(jvmDll); return 1; }

    std::string class_str = mainClassName;
    for (size_t p = 0; p < class_str.size(); p++) {
        if (class_str[p] == '.') class_str[p] = '/';
    }

    jclass mainClass = env->FindClass(class_str.c_str());
    if (!mainClass) {
        if (env->ExceptionCheck()) env->ExceptionDescribe();
        jvm->DestroyJavaVM(); FreeLibrary(jvmDll);
        if (SUCCEEDED(hr_com)) CoUninitialize();
        return 1;
    }

    jmethodID mainMethod = env->GetStaticMethodID(mainClass, "main", "([Ljava/lang/String;)V");
    if (!mainMethod) {
        jvm->DestroyJavaVM(); FreeLibrary(jvmDll);
        if (SUCCEEDED(hr_com)) CoUninitialize();
        return 1;
    }

    jclass stringClass = env->FindClass("java/lang/String");
    jobjectArray mainArgs = env->NewObjectArray(0, stringClass, NULL);

    env->CallStaticVoidMethod(mainClass, mainMethod, mainArgs);

    int exit_code = 0;
    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        exit_code = 1;
    }

    jvm->DestroyJavaVM();
    FreeLibrary(jvmDll);
    if (SUCCEEDED(hr_com)) CoUninitialize();
    return exit_code;
}
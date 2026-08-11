// =============================================================================
//  ACE Portrait Bridge - loader
// =============================================================================
//
//  Finds the running eu5.exe and loads ace_portrait_bridge.dll into it, so
//  users do not need Cheat Engine just to inject.
//
//  Usage:
//      ace_bridge_loader.exe                     inject into a running eu5.exe
//      ace_bridge_loader.exe --setup <eu5.exe>   remember where the game is
//      ace_bridge_loader.exe --launch            start the game in debug mode,
//                                                then inject
//      ace_bridge_loader.exe --dll <path>        use a DLL other than the one
//                                                next to this loader
//
//  Debug mode matters: the portrait editor is opened from the console (`pe`),
//  and the DEBUG hover box that shows a character's ID only appears in debug
//  mode. --launch starts the game with -debug_mode so you do not have to set
//  it in Steam's launch options.
//
//  How it works: allocate a buffer in the target process, write the DLL path
//  into it, then CreateRemoteThread on LoadLibraryA with that buffer as the
//  argument. This is the standard documented technique; kernel32 sits at the
//  same base in every process on a given boot, so the local address of
//  LoadLibraryA is valid in the target.
//
//  Build:  cl /nologo /EHsc /O2 /std:c++17 loader.cpp /link /OUT:ace_bridge_loader.exe
//
// =============================================================================

#define _CRT_SECURE_NO_WARNINGS

#include <windows.h>
#include <tlhelp32.h>
#include <cstdio>
#include <string>

// Double-clicking a console app closes the window the instant main() returns,
// so every exit path goes through here.
static int Finish(int code) {
    printf("\nPress Enter to close.");
    (void)getchar();
    return code;
}

// Is a module with this file name loaded in the target process?
//
// This is how success is confirmed. The obvious approach -- reading the remote
// thread's exit code -- does NOT work on x64: LoadLibraryA returns a 64-bit
// HMODULE, GetExitCodeThread returns a 32-bit DWORD, and the value is
// truncated. When the low 32 bits happen to be zero, a perfectly successful
// injection looks like a failure.
static bool ModuleLoaded(DWORD pid, const char* dllName) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid);
    if (snap == INVALID_HANDLE_VALUE) return false;

    MODULEENTRY32 me{};
    me.dwSize = sizeof(me);
    bool found = false;

    if (Module32First(snap, &me)) {
        do {
            if (_stricmp(me.szModule, dllName) == 0) { found = true; break; }
        } while (Module32Next(snap, &me));
    }
    CloseHandle(snap);
    return found;
}

// Where the game lives, remembered next to the loader so --launch works with
// no arguments after the first time.
static std::string ConfigPath() {
    char self[MAX_PATH] = {0};
    GetModuleFileNameA(nullptr, self, MAX_PATH);
    std::string p(self);
    const size_t slash = p.find_last_of("\\/");
    return (slash == std::string::npos ? std::string() : p.substr(0, slash + 1))
         + "ace_bridge.ini";
}

static std::string LoadGamePath() {
    FILE* f = fopen(ConfigPath().c_str(), "r");
    if (!f) return {};
    char buf[MAX_PATH] = {0};
    if (!fgets(buf, sizeof(buf), f)) { fclose(f); return {}; }
    fclose(f);
    std::string p(buf);
    while (!p.empty() && (p.back() == '\n' || p.back() == '\r' || p.back() == ' ')) p.pop_back();
    return p;
}

static bool SaveGamePath(const std::string& path) {
    FILE* f = fopen(ConfigPath().c_str(), "w");
    if (!f) return false;
    fprintf(f, "%s\n", path.c_str());
    fclose(f);
    return true;
}

static DWORD FindProcess(const wchar_t* exeName) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;

    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    DWORD pid = 0;

    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, exeName) == 0) { pid = pe.th32ProcessID; break; }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return pid;
}

int main(int argc, char** argv) {
    printf("ACE Portrait Bridge - loader\n\n");

    // --- arguments -------------------------------------------------------
    std::string dll, gamePath;
    bool wantLaunch = false;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--launch") {
            wantLaunch = true;
        } else if (a == "--setup" && i + 1 < argc) {
            gamePath = argv[++i];
            if (SaveGamePath(gamePath)) {
                printf("  remembered: %s\n", gamePath.c_str());
                printf("\n  From now on:  ace_bridge_loader.exe --launch\n");
            } else {
                printf("  could not write %s\n", ConfigPath().c_str());
            }
            return Finish(0);
        } else if (a == "--dll" && i + 1 < argc) {
            dll = argv[++i];
        } else if (a.size() && a[0] != '-') {
            dll = a;                       // bare path, for backwards compatibility
        }
    }

    // --- resolve the DLL path -------------------------------------------
    if (dll.empty()) {
        char self[MAX_PATH] = {0};
        GetModuleFileNameA(nullptr, self, MAX_PATH);
        std::string p(self);
        const size_t slash = p.find_last_of("\\/");
        dll = (slash == std::string::npos ? std::string() : p.substr(0, slash + 1))
            + "ace_portrait_bridge.dll";
    }

    char full[MAX_PATH] = {0};
    if (!GetFullPathNameA(dll.c_str(), MAX_PATH, full, nullptr)) {
        printf("  could not resolve the DLL path.\n");
        return Finish(1);
    }
    if (GetFileAttributesA(full) == INVALID_FILE_ATTRIBUTES) {
        printf("  DLL not found:\n    %s\n\n", full);
        printf("  Put ace_portrait_bridge.dll next to this loader, or pass its\n"
               "  path as an argument.\n");
        return Finish(1);
    }
    printf("  dll : %s\n", full);

    // --- optionally start the game in debug mode -------------------------
    DWORD pid = FindProcess(L"eu5.exe");

    if (!pid && wantLaunch) {
        std::string game = gamePath.empty() ? LoadGamePath() : gamePath;
        if (game.empty()) {
            printf("\n  I do not know where eu5.exe is. Tell me once:\n");
            printf("    ace_bridge_loader.exe --setup \"D:\\Steam\\steamapps\\common\\"
                   "Europa Universalis V\\binaries\\eu5.exe\"\n");
            return Finish(1);
        }

        printf("\n  launching with -debug_mode:\n    %s\n", game.c_str());

        std::string dir = game;
        const size_t slash = dir.find_last_of("\\/");
        if (slash != std::string::npos) dir = dir.substr(0, slash);

        std::string cmd = "\"" + game + "\" -debug_mode";

        STARTUPINFOA si{}; si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        if (!CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr,
                            dir.c_str(), &si, &pi)) {
            printf("  could not start it (error %lu).\n", GetLastError());
            printf("  Steam may need to be running first.\n");
            return Finish(1);
        }
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);

        printf("  waiting for it to come up");
        for (int i = 0; i < 60 && !pid; ++i) {
            Sleep(1000);
            printf(".");
            pid = FindProcess(L"eu5.exe");
        }
        printf("\n");
    }

    // --- find the game ---------------------------------------------------
    if (!pid) {
        printf("\n  eu5.exe is not running.\n");
        printf("  Start the game yourself, or use --launch to have this start it\n");
        printf("  in debug mode.\n");
        return Finish(1);
    }
    printf("  game: eu5.exe (pid %lu)\n\n", pid);

    HANDLE proc = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
                              PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
                              FALSE, pid);
    if (!proc) {
        printf("  could not open the process (error %lu).\n", GetLastError());
        printf("  Try running this loader as administrator.\n");
        return Finish(1);
    }

    // --- write the path into the target ---------------------------------
    const size_t len = strlen(full) + 1;
    void* remote = VirtualAllocEx(proc, nullptr, len, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote) {
        printf("  VirtualAllocEx failed (error %lu).\n", GetLastError());
        CloseHandle(proc);
        return Finish(1);
    }
    if (!WriteProcessMemory(proc, remote, full, len, nullptr)) {
        printf("  WriteProcessMemory failed (error %lu).\n", GetLastError());
        VirtualFreeEx(proc, remote, 0, MEM_RELEASE);
        CloseHandle(proc);
        return Finish(1);
    }

    // --- LoadLibraryA in the target -------------------------------------
    auto loadLib = reinterpret_cast<LPTHREAD_START_ROUTINE>(
        GetProcAddress(GetModuleHandleA("kernel32.dll"), "LoadLibraryA"));

    HANDLE thread = CreateRemoteThread(proc, nullptr, 0, loadLib, remote, 0, nullptr);
    if (!thread) {
        printf("  CreateRemoteThread failed (error %lu).\n", GetLastError());
        VirtualFreeEx(proc, remote, 0, MEM_RELEASE);
        CloseHandle(proc);
        return Finish(1);
    }

    WaitForSingleObject(thread, 10000);
    CloseHandle(thread);
    VirtualFreeEx(proc, remote, 0, MEM_RELEASE);
    CloseHandle(proc);

    // confirm by looking for the module in the target, not by the exit code
    const char* leaf = strrchr(full, '\\');
    leaf = leaf ? leaf + 1 : full;

    if (!ModuleLoaded(pid, leaf)) {
        printf("  the DLL does not appear in the process.\n");
        printf("  Usually an architecture mismatch -- the DLL must be x64.\n");
        printf("  Check ace_portrait_bridge.log next to eu5.exe as well; if it\n"
               "  has fresh lines, the injection actually worked.\n");
        return Finish(1);
    }

    printf("  injected.\n\n");
    printf("  Check ace_portrait_bridge.log next to eu5.exe -- both build\n"
           "  guards should say \"ok\" before you use the hotkeys.\n\n");
    printf("    Ctrl+Shift+L   character -> editor\n");
    printf("    Ctrl+Shift+S   editor -> character\n");
    printf("    Ctrl+Shift+R   forget cached addresses\n\n");
    printf("  Inject once per game launch. Restart the game before injecting a\n"
           "  new build -- two copies will both run and duplicate the log.\n");
    return Finish(0);
}

#undef UNICODE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <sstream>
#include <map>
#include <psapi.h>
#include <TlHelp32.h>
#include "starfallPayload.h"
#include "nullrhiPatch.h"
#include <thread>
#include <atomic>
#include <mutex>
#include <algorithm>
#include <chrono>
#include <signal.h>


// OMEN AUTO HSOTER BY KAI :)
// Made by someone who definitely knows what they're doing
// Also my ADDHD was kicking in so don't mind the comments

// RIP little processes, you will not be missed
void terminateProcesses(const char* target) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPALL, NULL);
    if (snapshot == INVALID_HANDLE_VALUE) return;

    PROCESSENTRY32 entry;
    entry.dwSize = sizeof(entry);
    BOOL hasEntry = Process32First(snapshot, &entry);
    while (hasEntry) {
        if (_stricmp(entry.szExeFile, target) == 0) {
            HANDLE processHandle = OpenProcess(PROCESS_TERMINATE, 0, entry.th32ProcessID);
            if (processHandle) {
                TerminateProcess(processHandle, 0); // BYE BYE FELICIA
                CloseHandle(processHandle);
            }
        }
        hasEntry = Process32Next(snapshot, &entry);
    }
    CloseHandle(snapshot);
}

// Global flag for shutdown
std::atomic<bool> globalShutdown(false);
std::vector<PROCESS_INFORMATION> activeProcesses;
std::mutex processMutex;

// Console handler to catch window closure
BOOL WINAPI ConsoleHandler(DWORD signal) {
    switch (signal) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:  // This happens when you click the X button
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
        std::cout << "\n[SYSTEM] Shutting down all servers...\n";
        globalShutdown = true;

        // Kill all running game processes
        {
            std::lock_guard<std::mutex> lock(processMutex);
            for (auto& proc : activeProcesses) {
                if (proc.hProcess != NULL) {
                    TerminateProcess(proc.hProcess, 0);
                    CloseHandle(proc.hProcess);
                    CloseHandle(proc.hThread);
                }
            }
            activeProcesses.clear();
        }

        // Also kill any remaining Fortnite processes just in case
        terminateProcesses("FortniteClient-Win64-Shipping.exe");
        terminateProcesses("FortniteClient-Win64-Shipping_EAC.exe");
        terminateProcesses("FortniteLauncher.exe");

        Sleep(1000);
        exit(0);
        return TRUE;
    }
    return FALSE;
}

// Like a dating profile but for game servers
struct AccountProfile {
    std::string username;
    std::string passkey;
    std::string variant;
    bool status;
};


// Global variables are scary apparently
struct ThreadContext {
    PROCESS_INFORMATION procData;
    HANDLE consolePipe;
    std::string identifier;
    std::string gameType;
    std::atomic<bool>* shutdownFlag;
};

// Splits a string like a banana very useful for parsing config files
std::vector<std::string> tokenize(const std::string& input, char delimiter) {
    std::stringstream stream(input);
    std::string segment;
    std::vector<std::string> output;
    while (std::getline(stream, segment, delimiter)) {
        output.push_back(segment);
    }
    return output;
}

// No needles required just questionable Windows API calls
bool loadLibraryIntoProcess(HANDLE process, const std::string& libraryPath) {
    const char* pathCStr = libraryPath.c_str();
    size_t pathLength = libraryPath.size() + 1;

    // Make sure the DLL actually exists before we try anything
    std::ifstream validator(libraryPath);
    if (validator.fail()) return !printf("Bruh I can't find %s!\n", pathCStr);
    validator.close();

    // Allocate memory in the target process
    void* remoteMemory = VirtualAllocEx(process, nullptr, pathLength, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remoteMemory) return false;

    WriteProcessMemory(process, remoteMemory, pathCStr, pathLength, NULL);

    // Create remote thread that loads our DLL
    HANDLE remoteOp = CreateRemoteThread(process, 0, 0, (LPTHREAD_START_ROUTINE)LoadLibraryA, remoteMemory, 0, 0);
    if (!remoteOp) {
        VirtualFreeEx(process, remoteMemory, 0, MEM_RELEASE);
        return false;
    }

    WaitForSingleObject(remoteOp, INFINITE); // Wait forever like waiting for you to climax

    // Cleanup so we don't leak every where
    CloseHandle(remoteOp);
    VirtualFreeEx(process, remoteMemory, pathLength, MEM_RELEASE);
    return true;
}

// Global settings storage
std::map<std::string, std::string> settings;
char directoryPath[MAX_PATH];
std::mutex outputSync; // Prevents messages from getting scrambled like eggs

// Thread safe console printing because nobody likes messy output
void logMessage(const std::string& account, const std::string& mode, const std::string& text) {
    std::lock_guard<std::mutex> guard(outputSync);
    std::cout << "[" << mode << "][" << account << "] " << text << std::endl;
}


// Like a lifeguard but for DLL injection :)
DWORD WINAPI monitorOutput(LPVOID param) {
    ThreadContext* ctx = (ThreadContext*)param;
    PROCESS_INFORMATION procInfo = ctx->procData;
    HANDLE readHandle = ctx->consolePipe;
    std::string accountName = ctx->identifier;
    std::string gameVariant = ctx->gameType;
    std::atomic<bool>* shutdownFlag = ctx->shutdownFlag;

    char buffer[4096];
    DWORD bytesAvailable;
    bool injected = false;

    while (!(*shutdownFlag)) {
        if (!ReadFile(readHandle, buffer, sizeof(buffer), &bytesAvailable, NULL)) break;
        if (bytesAvailable == 0) continue;

        if (!injected) {
            std::string output(buffer, bytesAvailable);

            // Look for the CreatingParty message I don't know why ask stranger he snart
            if (output.find("CreatingParty") != std::string::npos) {
                std::string targetDLL = (gameVariant == "BR") ? settings["br"] : settings["lg"];
                if (targetDLL.find(':') == std::string::npos) {
                    targetDLL = std::string(directoryPath) + "\\" + targetDLL;
                }

                if (!loadLibraryIntoProcess(procInfo.hProcess, targetDLL)) {
                    logMessage(accountName, gameVariant, "DLL injection failed! Something went wrong :(");
                    TerminateProcess(procInfo.hProcess, 0);
                    CloseHandle(procInfo.hProcess);
                    CloseHandle(procInfo.hThread);
                    delete ctx;
                    return 1;
                }
                logMessage(accountName, gameVariant, "Game module loaded!");
                injected = true;
            }
            // Check if login failed because some of use are shit typers
            else if (output.find("[UOnlineAccountCommon::ForceLogout]") != std::string::npos) {
                size_t reasonPos = output.find("reason \"");
                if (reasonPos != std::string::npos) {
                    reasonPos += 8;
                    size_t endPos = output.find("\"", reasonPos);
                    if (endPos != std::string::npos) {
                        logMessage(accountName, gameVariant, "Login failed: " + output.substr(reasonPos, endPos - reasonPos));
                    }
                }
                TerminateProcess(procInfo.hProcess, 0);
                CloseHandle(procInfo.hProcess);
                CloseHandle(procInfo.hThread);
                injected = true;
            }
        }
    }

    delete ctx;
    return 0;
}

// This function does all the heavy lifting like me I know I'm tuff aye
void deployInstance(const AccountProfile& profile, const std::string& installPath, const std::string& endpoint,
    const std::string& patchDLL, const std::string& payloadDLL, std::atomic<bool>& shutdownFlag) {

    // Setup security attributes for our pipe (boring but necessary)
    SECURITY_ATTRIBUTES security = { sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };
    HANDLE outputRead = nullptr, outputWrite = nullptr;

    // Create a pipe to capture console output (like a straw for text)
    if (!CreatePipe(&outputRead, &outputWrite, &security, 0)) {
        logMessage(profile.username, profile.variant, "Pipe creation failed! Windows being windows classic");
        return;
    }
    SetHandleInformation(outputRead, HANDLE_FLAG_INHERIT, 0);

    // Configure startup info so we can capture that sweet console output
    STARTUPINFOA startup = { sizeof(startup) };
    startup.hStdOutput = outputWrite;
    startup.dwFlags = STARTF_USESTDHANDLES;

    bool initialLaunch = true;
    bool keepRunning = true;

    while (keepRunning && profile.status && !shutdownFlag) {
        logMessage(profile.username, profile.variant, initialLaunch ? "Spinning up server..." : "Rebooting instance (it crashed or something)...");
        initialLaunch = false;

        // Thanks to Phoenix for the thingys
        std::string executable = installPath + "\\FortniteGame\\Binaries\\Win64\\FortniteClient-Win64-Shipping.exe";
        std::string arguments = " -epicapp=Fortnite -epicenv=Prod -epicportal -skippatchcheck -nobe";
        arguments += " -fromfl=eac -fltoken=3db3ba5dcbd2e16703f3978d";
        arguments += " -caldera=eyJhbGciOiJFUzI1NiIsInR5cCI6IkpXVCJ9.eyJhY2NvdW50X2lkIjoiYmU5ZGE1YzJmYmVhNDQwN2IyZjQwZWJhYWQ4NTlhZDQiLCJnZW5lcmF0ZWQiOjE2Mzg3MTcyNzgsImNhbGRlcmFHdWlkIjoiMzgxMGI4NjMtMmE2NS00NDU3LTliNTgtNGRhYjNiNDgyYTg2IiwiYWNQcm92aWRlciI6IkVhc3lBbnRpQ2hlYXQiLCJub3RlcyI6IiIsImZhbGxiYWNrIjpmYWxzZX0.VAWQB67RTxhiWOxx7DBjnzDnXyyEnX7OljJm-j2d88G_WgwQ9wrE6lwMEHZHjBd1ISJdUO1UVUqkfLdU5nofBQ";
        arguments += " -nullrhi -nosound -nosplash";
        arguments += " -AUTH_LOGIN=" + profile.username;
        arguments += " -AUTH_PASSWORD=" + profile.passkey;
        arguments += " -AUTH_TYPE=epic";
        arguments += " -backend=" + endpoint;

        std::string fullCommand = executable + arguments;

        PROCESS_INFORMATION procInfo;
        // Create the process
        if (!CreateProcessA(NULL, (char*)fullCommand.c_str(), NULL, NULL, TRUE, CREATE_SUSPENDED, NULL, installPath.c_str(), &startup, &procInfo)) {
            logMessage(profile.username, profile.variant, "Failed to start process! Error code: " + std::to_string(GetLastError()));
            break;
        }

        // Add to active processes list for cleanup
        {
            std::lock_guard<std::mutex> lock(processMutex);
            activeProcesses.push_back(procInfo);
        }

        logMessage(profile.username, profile.variant, "Process created! PID: " + std::to_string(procInfo.dwProcessId));

        // Inject NullrhiPatch (whatever that does... probably magic)
        if (!loadLibraryIntoProcess(procInfo.hProcess, patchDLL)) {
            logMessage(profile.username, profile.variant, "Patch injection failed!");
            TerminateProcess(procInfo.hProcess, 0);
            {
                std::lock_guard<std::mutex> lock(processMutex);
                auto it = std::find_if(activeProcesses.begin(), activeProcesses.end(),
                    [&](const PROCESS_INFORMATION& p) { return p.dwProcessId == procInfo.dwProcessId; });
                if (it != activeProcesses.end()) activeProcesses.erase(it);
            }
            CloseHandle(procInfo.hProcess);
            CloseHandle(procInfo.hThread);
            break;
        }
        logMessage(profile.username, profile.variant, "Patch injected!");

        // Resume all threads
        HANDLE threadSnap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (threadSnap != INVALID_HANDLE_VALUE) {
            THREADENTRY32 te = { sizeof(THREADENTRY32) };
            if (Thread32First(threadSnap, &te)) {
                do {
                    if (te.th32OwnerProcessID == procInfo.dwProcessId) {
                        HANDLE threadHandle = OpenThread(THREAD_SUSPEND_RESUME, FALSE, te.th32ThreadID);
                        if (threadHandle) {
                            ResumeThread(threadHandle);
                            CloseHandle(threadHandle);
                        }
                    }
                } while (Thread32Next(threadSnap, &te));
            }
            CloseHandle(threadSnap);
        }
        logMessage(profile.username, profile.variant, "Threads resumed!");

        // Inject Starfall the main payload because well plooshi is trans but still smart sometimes
        if (!loadLibraryIntoProcess(procInfo.hProcess, payloadDLL)) {
            logMessage(profile.username, profile.variant, "Payload injection failed! This is fine... everything is fine...");
            TerminateProcess(procInfo.hProcess, 0);
            {
                std::lock_guard<std::mutex> lock(processMutex);
                auto it = std::find_if(activeProcesses.begin(), activeProcesses.end(),
                    [&](const PROCESS_INFORMATION& p) { return p.dwProcessId == procInfo.dwProcessId; });
                if (it != activeProcesses.end()) activeProcesses.erase(it);
            }
            CloseHandle(procInfo.hProcess);
            CloseHandle(procInfo.hThread);
            break;
        }
        logMessage(profile.username, profile.variant, "Payload injected!");

        // Create monitoring thread to watch console output
        ThreadContext* ctx = new ThreadContext();
        ctx->procData = procInfo;
        ctx->consolePipe = outputRead;
        ctx->identifier = profile.username;
        ctx->gameType = profile.variant;
        ctx->shutdownFlag = &shutdownFlag;

        HANDLE monitor = CreateThread(NULL, 0, monitorOutput, ctx, 0, NULL);

        logMessage(profile.username, profile.variant, "Waiting for game process");

        // Wait for the game to exit or shutdown
        WaitForSingleObject(procInfo.hProcess, INFINITE);

        // Remove from active processes on exit
        {
            std::lock_guard<std::mutex> lock(processMutex);
            auto it = std::find_if(activeProcesses.begin(), activeProcesses.end(),
                [&](const PROCESS_INFORMATION& p) { return p.dwProcessId == procInfo.dwProcessId; });
            if (it != activeProcesses.end()) activeProcesses.erase(it);
        }

        DWORD exitCode = 0;
        GetExitCodeProcess(procInfo.hProcess, &exitCode);
        logMessage(profile.username, profile.variant, "Process exited with code: " + std::to_string(exitCode));

        TerminateThread(monitor, 0);
        CloseHandle(monitor);
        CloseHandle(procInfo.hProcess);
        CloseHandle(procInfo.hThread);

        if (!shutdownFlag) {
            Sleep(5000); // Give it a 5 second breather before restarting
        }
    }

    CloseHandle(outputRead);
    CloseHandle(outputWrite);
}

int main() {
    // Set up console handler to catch window closure
    if (!SetConsoleCtrlHandler(ConsoleHandler, TRUE)) {
        printf("Warning: Could not set console handler!\n");
    }

    printf("============================================\n");
    printf("   OMEN AUTO HOSTER 1.0\n");
    printf("============================================\n\n");

    // Load configuration file
    std::ifstream configFile("config.ini");
    if (!configFile.is_open()) {
        // Config doesn't exist create a default one
        std::ofstream defaultConfig("config.ini");
        defaultConfig <<
            "# Fortnite path\n"
            "path=\n"
            "# Backned URL\n"
            "backend=\n"
            "# Battle Royale DLL\n"
            "br=\n"
            "# Lategame DLL\n"
            "lg=\n"
            "# Host Battle Royale\n"
            "br_enabled=true\n"
            "# Host Lategame\n"
            "lg_enabled=true\n\n"
            "# add more with br_user2, br_pass2 to host more BR servers\n"
            "br_user1=\n"
            "br_pass1=\n\n"
            "# add more with lg_user2, lg_pass2 to host more LG servers\n"
            "lg_user1=\n"
            "lg_pass1=\n";
        defaultConfig.close();
        printf("No config.ini found! One was created, Go fill it out and try again!\n");
        system("pause");
        return 1;
    }

    // Parse the config file line by line (like reading a book, but less fun)
    std::string rawLine;
    while (std::getline(configFile, rawLine)) {
        if (rawLine.empty() || rawLine[0] == '#') continue;
        auto parts = tokenize(rawLine, '=');
        if (parts.size() >= 2) {
            // Trim whitespace just in case like what we did to the blacks
            std::string key = parts[0];
            std::string value = parts[1];
            // Remove any garbage
            if (!value.empty() && value.back() == '\r') value.pop_back();
            settings[key] = value;
        }
    }
    configFile.close();

    // Validate required settings (no skipping leg day)
    if (settings.find("path") == settings.end() || settings.find("backend") == settings.end()) {
        printf("Your config is missing 'path' or 'backend'! Read the instructions!\n");
        system("pause");
        return 1;
    }

    // Check which game modes are enabled
    bool brActive = settings["br_enabled"] == "true";
    bool lgActive = settings["lg_enabled"] == "true";

    if (!brActive && !lgActive) {
        printf("You need to enable at least BR or LG mode! What are you even trying to do?\n");
        system("pause");
        return 1;
    }

    // Verify DLL paths exist if their modes are enabled
    if (brActive && (settings.find("br") == settings.end() || settings["br"].empty())) {
        printf("BR mode is on but br path is empty! Fix it!\n");
        system("pause");
        return 1;
    }

    if (lgActive && (settings.find("lg") == settings.end() || settings["lg"].empty())) {
        printf("LG mode is on but lg path is empty! Fix it!\n");
        system("pause");
        return 1;
    }

    // Load all the accounts (gotta catch 'em all) pokimon is shit btw
    std::vector<AccountProfile> profiles;
    profiles.reserve(16); // Pre allocate for performance

    if (brActive) {
        for (int i = 1; ; i++) {
            std::string userKey = "br_user" + std::to_string(i);
            std::string passKey = "br_pass" + std::to_string(i);

            if (settings.find(userKey) != settings.end() && !settings[userKey].empty() &&
                settings.find(passKey) != settings.end() && !settings[passKey].empty()) {
                AccountProfile profile;
                profile.username = settings[userKey];
                profile.passkey = settings[passKey];
                profile.variant = "BR";
                profile.status = true;
                profiles.push_back(profile);
                printf("[+] Loaded BR account: %s\n", settings[userKey].c_str());
            }
            else break;
        }
    }

    if (lgActive) {
        for (int i = 1; ; i++) {
            std::string userKey = "lg_user" + std::to_string(i);
            std::string passKey = "lg_pass" + std::to_string(i);

            if (settings.find(userKey) != settings.end() && !settings[userKey].empty() &&
                settings.find(passKey) != settings.end() && !settings[passKey].empty()) {
                AccountProfile profile;
                profile.username = settings[userKey];
                profile.passkey = settings[passKey];
                profile.variant = "LG";
                profile.status = true;
                profiles.push_back(profile);
                printf("[+] Loaded LG account: %s\n", settings[userKey].c_str());
            }
            else break;
        }
    }

    if (profiles.empty()) {
        printf("No accounts found! Add some to your config.ini\n");
        printf("Example: br_user1=your@email.com and br_pass1=yourpassword\n");
        system("pause");
        return 1;
    }

    // Display what we're about to launch (drum roll please)
    printf("\n========== Infomation ==========\n");
    int brCount = 0, lgCount = 0;
    for (const auto& p : profiles) {
        if (p.variant == "BR") brCount++;
        else if (p.variant == "LG") lgCount++;
    }
    printf("BR Servers: %d (Enabled: %s)\n", brCount, brActive ? "YES" : "NO");
    printf("LG Servers: %d (Enabled: %s)\n", lgCount, lgActive ? "YES" : "NO");
    printf("Total instances: %zu\n", profiles.size());
    printf("=========================================\n\n");

    // Verify Fortnite installation exists
    std::string gamePath = settings["path"];
    std::ifstream exeCheck(gamePath + "\\FortniteGame\\Binaries\\Win64\\FortniteClient-Win64-Shipping.exe");
    if (!exeCheck.is_open()) {
        printf("Can't find Fortnite at that path! Did you install it there?\n");
        system("pause");
        return 1;
    }
    exeCheck.close();

    // Extract payloads to temp directory
    char tempBuffer[MAX_PATH];
    GetTempPathA(MAX_PATH, tempBuffer);

    std::string payloadPath = std::string(tempBuffer) + "\\game_runtime.dll";
    std::ofstream payloadOut(payloadPath, std::ios::binary);
    payloadOut.write((const char*)Starfall, sizeof(Starfall));
    payloadOut.close();

    std::string patchPath = std::string(tempBuffer) + "\\runtime_patch.dll";
    std::ofstream patchOut(patchPath, std::ios::binary);
    patchOut.write((const char*)NullrhiPatch, sizeof(NullrhiPatch));
    patchOut.close();

    // Kill any existing Fortnite processes
    terminateProcesses("FortniteClient-Win64-Shipping.exe");
    terminateProcesses("FortniteClient-Win64-Shipping_EAC.exe");
    terminateProcesses("FortniteLauncher.exe");

    // Launch helper processes
    STARTUPINFOA siEAC = { sizeof(STARTUPINFOA) };
    PROCESS_INFORMATION piEAC;
    CreateProcessA((gamePath + "\\FortniteGame\\Binaries\\Win64\\FortniteClient-Win64-Shipping_EAC.exe").c_str(),
        (char*)"", NULL, NULL, TRUE, CREATE_NO_WINDOW | CREATE_SUSPENDED, NULL, gamePath.c_str(), &siEAC, &piEAC);

    STARTUPINFOA siLauncher = { sizeof(STARTUPINFOA) };
    PROCESS_INFORMATION piLauncher;
    CreateProcessA((gamePath + "\\FortniteGame\\Binaries\\Win64\\FortniteLauncher.exe").c_str(),
        (char*)"", NULL, NULL, TRUE, CREATE_NO_WINDOW | CREATE_SUSPENDED, NULL, gamePath.c_str(), &siLauncher, &piLauncher);

    GetCurrentDirectoryA(MAX_PATH, directoryPath);

    printf("=== Launching %zu Game Servers ===\n", profiles.size());
    printf("=== Close this window to stop all servers ===\n\n");

    auto startTime = std::chrono::steady_clock::now();

    // Launch each server in its own thread (because we're in the big 26 with big standards
    std::vector<std::thread> threadPool;
    threadPool.reserve(profiles.size());

    for (const auto& profile : profiles) {
        threadPool.emplace_back(deployInstance, profile, gamePath, settings["backend"], patchPath, payloadPath, std::ref(globalShutdown));
        Sleep(1500);
    }

    // Wait for all servers to finish or shutdown
    for (auto& thread : threadPool) {
        thread.join();
    }

    auto endTime = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);

    if (globalShutdown) {
        printf("\n=== Shutdown complete. All servers terminated ===\n");
    }
    else {
        printf("\n=== All servers launched in %lld ms ===\n", duration.count());
    }

    // Cleanup and they dip like your father
    TerminateProcess(piEAC.hProcess, 0);
    TerminateProcess(piLauncher.hProcess, 0);
    CloseHandle(piEAC.hProcess);
    CloseHandle(piEAC.hThread);
    CloseHandle(piLauncher.hProcess);
    CloseHandle(piLauncher.hThread);

    return 0;
}
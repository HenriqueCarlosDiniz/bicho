#include "pc_process.h"

#if !defined(ARDUINO)
#include <cstdlib>
#include <string>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

bool spawnBackgroundChildProcess(const char* command_line) {
    // Job Object compartilhado: fechado pelo Windows quando o emulador termina -> mata todos os filhos
    static HANDLE child_job = nullptr;
    if (child_job == nullptr) {
        child_job = CreateJobObjectA(nullptr, nullptr);
        if (child_job != nullptr) {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {};
            limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            SetInformationJobObject(child_job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
        }
    }

    std::string cmd(command_line);
    STARTUPINFOA startup_info = {};
    startup_info.cb = sizeof(startup_info);
    PROCESS_INFORMATION proc_info = {};
    BOOL created = CreateProcessA(nullptr, &cmd[0], nullptr, nullptr, FALSE,
                                  CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, nullptr,
                                  &startup_info, &proc_info);
    if (!created) {
        return false;
    }
    if (child_job != nullptr) {
        AssignProcessToJobObject(child_job, proc_info.hProcess);
    }
    ResumeThread(proc_info.hThread);
    CloseHandle(proc_info.hThread);
    CloseHandle(proc_info.hProcess);
    return true;
}

#else

bool spawnBackgroundChildProcess(const char* command_line) {
    std::string cmd = std::string(command_line) + " >/dev/null 2>&1 &";
    return std::system(cmd.c_str()) == 0;
}

#endif
#endif // !defined(ARDUINO)

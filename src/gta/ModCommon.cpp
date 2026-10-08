#include "ModCommon.h"

#include <cstdarg>
#include <ctime>

#include "CTimer.h"

namespace mc {

std::string GameDir() {
    static std::string dir;
    if (dir.empty()) {
        char buf[MAX_PATH] = {};
        GetModuleFileNameA(nullptr, buf, MAX_PATH);
        dir = buf;
        size_t slash = dir.find_last_of("\\/");
        if (slash != std::string::npos)
            dir.resize(slash);
    }
    return dir;
}

std::string ModPath(const char* file) {
    return GameDir() + "\\MinecraftSA\\" + file;
}

void Log(const char* fmt, ...) {
    static FILE* f = nullptr;
    static bool tried = false;
    if (!f && !tried) {
        tried = true;
        CreateDirectoryA((GameDir() + "\\MinecraftSA").c_str(), nullptr);
        f = fopen(ModPath("MinecraftSA.log").c_str(), "w");
    }
    if (!f)
        return;
    char time_buf[32];
    time_t now = time(nullptr);
    strftime(time_buf, sizeof(time_buf), "%H:%M:%S", localtime(&now));
    fprintf(f, "[%s] ", time_buf);
    va_list args;
    va_start(args, fmt);
    vfprintf(f, fmt, args);
    va_end(args);
    fputc('\n', f);
    fflush(f);
}

float FrameDelta() {
    // ms_fTimeStep is in 1/50 s units
    return Clamp(CTimer::ms_fTimeStep / 50.0f, 0.0f, 0.1f);
}

} // namespace mc

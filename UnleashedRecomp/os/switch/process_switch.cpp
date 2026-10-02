#include <os/process.h>

#include <cctype>
#include <string>

extern "C"
{
    // libnx: the homebrew loader's arguments, set up by argvSetup() before the static constructors run.
    extern int __system_argc;
    extern char** __system_argv;
}

namespace
{
    // [Switch] The game lives in the NRO's own folder, wherever that is on the SD card (sdmc:/switch/UnleashedRecomp,
    // sdmc:/switch/SonicUnleashed, ...): the game files, config.toml, the saves and the caches are all next to it. The
    // homebrew menu and forwarders pass the NRO's path as argv[0]; without a usable one, the usual folder.
    constexpr const char* DEFAULT_APP_PATH = "sdmc:/switch/UnleashedRecomp/UnleashedRecomp.nro";

    std::string FindAppPath()
    {
        if (__system_argc < 1 || __system_argv == nullptr || __system_argv[0] == nullptr)
            return DEFAULT_APP_PATH;

        std::string path = __system_argv[0];
        for (char& c : path)
        {
            if (c == '\\')
                c = '/';
        }

        // Some loaders give the path without its device.
        if (!path.empty() && path[0] == '/')
            path = "sdmc:" + path;

        std::string lower = path;
        for (char& c : lower)
            c = char(std::tolower(static_cast<unsigned char>(c)));

        const bool onSdCard = lower.rfind("sdmc:/", 0) == 0;
        const bool isNro = lower.size() > 4 && lower.compare(lower.size() - 4, 4, ".nro") == 0;
        if (!onSdCard || !isNro)
            return DEFAULT_APP_PATH;

        return path;
    }

    // Function-local: the static constructors of other files (user/paths.cpp) ask for it.
    const std::string& AppPath()
    {
        static const std::string path = FindAppPath();
        return path;
    }
}

std::filesystem::path os::process::GetExecutablePath()
{
    return AppPath();
}

std::filesystem::path os::process::GetExecutableRoot()
{
    const std::string& path = AppPath();
    const size_t slash = path.rfind('/');

    // An NRO at the root of the SD card: the root itself.
    return slash <= 5 ? std::string("sdmc:/") : path.substr(0, slash);
}

std::filesystem::path os::process::GetWorkingDirectory()
{
    std::error_code ec;
    auto path = std::filesystem::current_path(ec);
    return ec ? GetExecutableRoot() : path;
}

bool os::process::SetWorkingDirectory(const std::filesystem::path& path)
{
    std::error_code ec;
    std::filesystem::current_path(path, ec);
    return !ec;
}

bool os::process::StartProcess(const std::filesystem::path& path, const std::vector<std::string>& args, std::filesystem::path work)
{
    (void)path;
    (void)args;
    (void)work;
    return false;
}

void os::process::CheckConsole()
{
    g_consoleVisible = true;
}

void os::process::ShowConsole()
{
    g_consoleVisible = true;
}
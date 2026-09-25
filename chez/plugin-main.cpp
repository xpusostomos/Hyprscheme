#include <src/plugins/PluginAPI.hpp>
#include "SchemeHost.hpp"
#include <src/debug/crash/CrashReporter.hpp>
#include <csignal>
#include <cstring>
#include <unistd.h>

namespace Config::Scheme {
    void init();
    void shutdown();
}

// Chez displaces Hyprland's crash reporter when its kernel initializes; a
// plugin cannot edit Compositor.cpp to restore it, so reimplement the
// handler via the exported CrashReporter::createAndSaveCrash. Installed
// from SchemeManager's init (after Sbuild_heap).
static void schemeCrashHandler(int sig) {
    signal(SIGABRT, SIG_DFL);
    signal(SIGSEGV, SIG_DFL);
    signal(SIGALRM, [](int) {
        const char* m = "\nCrashReporter exceeded timeout, forcefully exiting\n";
        [[maybe_unused]] auto w = write(2, m, strlen(m));
        abort();
    });
    alarm(15);
    CrashReporter::createAndSaveCrash(sig);
    abort();
}

APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    Config::Scheme::init();
    // both artifacts register the name "scheme" (Hyprland keys plugins by
// handle, the name is hyprctl display metadata); the description tells the
// backends apart
    return {"scheme", std::string("Scheme scripting for Hyprland [") + SchemeHost::backendName() + "]", "Chris", "1.0"};
}

APICALL EXPORT void PLUGIN_EXIT() {
    Config::Scheme::shutdown();
}

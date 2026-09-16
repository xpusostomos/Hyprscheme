#pragma once

#include <unordered_map>

#include <src/desktop/DesktopTypes.hpp>

/*
    State shared between SchemeManager.cpp and SchemeLayout.cpp.
    Defined in SchemeManager.cpp, namespace Config::Scheme.
*/

namespace Config::Scheme::Internals {
    extern bool g_up;         // interpreter + bootstrap ready
    extern int  g_nextBindId; // id counter shared by binds/timers/layouts
    extern int  g_nextWindowId;
    extern std::unordered_map<int, PHLWINDOWREF> g_windows; // id -> weak window ref
}

#pragma once

#include <src/desktop/DesktopTypes.hpp>

/*
    State shared between SchemeManager.cpp and SchemeLayout.cpp.
    Defined in SchemeManager.cpp, namespace Config::Scheme.
*/

namespace Config::Scheme::Internals {
    extern bool g_up;         // interpreter + bootstrap ready
    extern int  g_nextBindId; // id counter shared by binds/timers/layouts

    // mints a window handle: a heap-allocated weak ref whose address crosses
    // to Scheme inside a guardian cell (see SchemeManager.cpp — the guardian
    // deletes it when the Scheme record dies)
    uintptr_t mintWindowHandle(PHLWINDOW window);
}

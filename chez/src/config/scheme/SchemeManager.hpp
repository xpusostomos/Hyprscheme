#pragma once

/*
    Experimental Chez Scheme scripting for Hyprland.

    Embeds the Chez interpreter (linked statically from the system Chez
    install) and loads $XDG_CONFIG_HOME/hypr/hyprland.scm if present.
    The user file is (re)loaded after every config (re)load via the
    config.reloaded event, and watched for changes with inotify.

    Everything lives in this directory; the only hook into the rest of
    Hyprland is the Scheme::init() call from Config::initConfigManager().
*/

namespace Config::Scheme {
    void init();
}

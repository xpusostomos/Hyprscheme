/*
    The notification family.

    One translation unit per object family (§4.7 Step G): this file owns both
    the entry points and their registration, so nothing outside it needs them
    declared, and SchemeInternals.hpp carries what families share.
*/

#include "Guile.hpp"
#include "Bindings.hpp"
#include "Handles.hpp"
#include "SThunkRef.hpp"
#include "SchemeInternals.hpp"

#include <src/notification/Notification.hpp>
#include <src/notification/NotificationOverlay.hpp>
// ParserUtils::parseColor — the compositor's own colour-string parser, used by
// hl--c-string-color rather than a hand-rolled one. It is the SAME parser the
// config file, the window-rule engine, the IPC commands and the scripting API
// all use, so our acceptance set matches theirs by construction.
#include <src/config/shared/parserUtils/ParserUtils.hpp>

namespace Config::Scheme {

    using namespace Internals;

    // ---- notifications: shared field parsing (used by hl-notify! AND the
    // live notification objects) ------------------------------------------------
    // icon names, mirroring the lua config's table
    static eIcons schemeIconFromStr(const std::string& ic, bool* ok = nullptr) {
        static const std::pair<const char*, eIcons> ICON_NAMES[] = {
            {"warning", ICON_WARNING}, {"warn", ICON_WARNING},     {"info", ICON_INFO},       {"hint", ICON_HINT},
            {"error", ICON_ERROR},     {"err", ICON_ERROR},       {"confused", ICON_CONFUSED},
            {"question", ICON_CONFUSED}, {"ok", ICON_OK},         {"none", ICON_NONE},
        };
        for (const auto& [n, i] : ICON_NAMES)
            if (ic == n)
                return i;
        if (ok)
            *ok = false;
        return ICON_NONE;
    }

    static eIcons schemeIconFromScheme(SCM v, bool* ok = nullptr) {
        if (scm_is_string(v))
            return schemeIconFromStr(schemeDatumToStr(v), ok);
        if (scm_is_integer(v)) {
            const auto raw = scm_to_int64(v);
            if (raw >= ICON_WARNING && raw <= ICON_NONE)
                return sc<eIcons>(raw);
        }
        if (ok)
            *ok = false;
        return ICON_NONE;
    }

    // A COLOUR IS A NUMBER. 0xAARRGGBB — alpha FIRST, then red, green, blue,
    // which is the packed-ARGB32 layout CHyprColor reads in its uint64_t
    // constructor (Color.cpp: ALPHA >>24, RED >>16, GREEN >>8, BLUE). So opaque
    // orange is #xffffa500, NOT #xffa500ff.
    //
    // Scheme has native hex integers, so a config writes the number directly and
    // no parsing is needed: (hl-notify! "hi" 5000 #:color #xffffa500). That is
    // the whole interface — the string spellings Hyprland's config syntax allows
    // (#rgb, #rrggbbaa, rgba(...), "0x…") exist for the CONFIG FILE and for the
    // scripting API's parity, and are reached deliberately through string->color
    // rather than by every colour argument quietly accepting them.
    //
    // A string still REACHES here from one place: hl--c-string-color, the
    // string->color gsubr. It goes to the compositor's own ParserUtils::parseColor,
    // deliberately NOT a hand-rolled parser of our own — the same argument as
    // resolving key names with the compositor's keymap resolver. Anything that
    // parser rejects, we reject, and vice versa: no invented spellings in either
    // direction. (Our old parser took bare hex digits like "FFA500FF" that
    // Hyprland rejects, so we accepted a string no config file could contain.)
    static std::optional<CHyprColor> colorFromNumber(SCM v) {
        if (!scm_is_integer(v))
            return std::nullopt;
        return CHyprColor(static_cast<uint64_t>(scm_to_uint64(v)));
    }

    static std::optional<CHyprColor> colorFromString(const std::string& cs) {
        // the compositor's parser, not ours. `0x…`, `#rgb`, `#rrggbb`,
        // `#rrggbbaa`, `rgba(...)`, `rgb(...)`, comma forms, bare decimal.
        const auto parsed = Config::ParserUtils::parseColor(cs);
        if (!parsed)
            return std::nullopt;
        return CHyprColor(static_cast<uint64_t>(*parsed));
    }

    static SCM hlNotify(const char* text, double durationMs, const char* icon, SCM color, double fontSize) {
        if (!g_up)
            return SCM_BOOL_F;

        eIcons     theIcon = ICON_NONE;
        const auto ic      = schemeIconFromStr(icon ? icon : "");
        if (icon && *icon) {
            bool ok = true;
            theIcon = schemeIconFromStr(icon, &ok);
            if (!ok) {
                g_configError = "hl-notify!: bad icon (expected none/warn/info/hint/error/confused/ok)";
                return SCM_BOOL_F;
            }
        }
        // ABSENT means "the icon's default colour", and that is #f rather than 0
        // so no valid colour is ambiguous; 0 is still accepted, as CHyprColor(0),
        // which is what the compositor's own default is.
        const auto col = scm_is_false(color) ? std::optional<CHyprColor>{CHyprColor(0)} : colorFromNumber(color);
        if (!col) {
            g_configError = "hl-notify!: bad 'color (expected a 0xAARRGGBB integer)";
            return SCM_BOOL_F;
        }
        Notification::overlay()->addNotification(text ? text : "", *col, sc<float>(durationMs), theIcon, sc<float>(fontSize));
        return SCM_BOOL_T;
    }

    // ---- live notification objects (upstream hl.notification parity) -----------
    // A notification handle is a hl::SNotificationHandle (Handles.hpp): the
    // weak ref plus the per-handle 'paused' bit.
    static SCM hlNotificationAdd(SCM fields) {
        if (!g_up)
            return SCM_BOOL_F;

        // walk the plist first: a bad field writes nothing (device-add parity)
        std::string text;
        double      timeout = -1, fontSize = 13.0;
        eIcons      icon = ICON_NONE;
        CHyprColor  color(0);
        SCM         l = fields;
        while (scm_is_pair(l) && scm_is_pair(scm_cdr(l))) {
            const std::string k = schemeDatumToStr(scm_car(l));
            const SCM         v = scm_car(scm_cdr(l));
            if (k == "text") {
                if (!scm_is_string(v)) {
                    g_configError = "hl-notification-add!: 'text must be a string";
                    return SCM_BOOL_F;
                }
                text = schemeDatumToStr(v);
            } else if (k == "timeout" || k == "duration" || k == "time") {
                if (!scm_is_integer(v) && !hl::isInexact(v)) {
                    g_configError = "hl-notification-add!: 'timeout must be a number (ms)";
                    return SCM_BOOL_F;
                }
                timeout = scm_is_integer(v) ? sc<double>(scm_to_int64(v)) : scm_to_double(v);
                if (timeout < 0) {
                    g_configError = "hl-notification-add!: 'timeout must be >= 0";
                    return SCM_BOOL_F;
                }
            } else if (k == "icon") {
                bool ok = true;
                icon = schemeIconFromScheme(v, &ok);
                if (!ok) {
                    g_configError = "hl-notification-add!: bad 'icon (expected none/warn/info/hint/error/confused/ok or an id 0-6)";
                    return SCM_BOOL_F;
                }
            } else if (k == "color") {
                const auto c = colorFromNumber(v);
                if (!c) {
                    g_configError = "hl-notification-add!: bad 'color (expected a 0xAARRGGBB integer)";
                    return SCM_BOOL_F;
                }
                color = *c;
            } else if (k == "font-size") {
                if (!scm_is_integer(v) && !hl::isInexact(v)) {
                    g_configError = "hl-notification-add!: 'font-size must be a number";
                    return SCM_BOOL_F;
                }
                fontSize = scm_is_integer(v) ? sc<double>(scm_to_int64(v)) : scm_to_double(v);
                if (fontSize <= 0) {
                    g_configError = "hl-notification-add!: 'font-size must be > 0";
                    return SCM_BOOL_F;
                }
            } else {
                g_configError = std::format("hl-notification-add!: unknown field '{}'", k);
                return SCM_BOOL_F;
            }
            l = scm_cdr(scm_cdr(l));
        }
        if (text.empty()) {
            g_configError = "hl-notification-add!: 'text is required";
            return SCM_BOOL_F;
        }
        if (timeout < 0) {
            g_configError = "hl-notification-add!: 'timeout is required";
            return SCM_BOOL_F;
        }

        const auto n = Notification::overlay()->addNotification(text, color, sc<float>(timeout), icon, sc<float>(fontSize));
        if (!n)
            return SCM_BOOL_F;
        return hl::notificationHandle(n);
    }

    static SCM hlNotificationList() {
        if (!g_up)
            return SCM_BOOL_F;
        std::vector<SCM> ids;
        for (const auto& n : Notification::overlay()->getNotifications())
            ids.push_back(hl::notificationHandle(n));
        return ids.empty() ? SCM_BOOL_F : hl::listOf(ids);
    }

    static SCM hlNotificationText(SCM id) {
        if (!g_up)
            return SCM_BOOL_F;
        const auto n = hl::notificationOf(id);
        if (!n)
            return SCM_BOOL_F;
        return scm_from_utf8_stringn(n->text().c_str(), n->text().size());
    }

    static SCM hlNotificationTimeout(SCM id) {
        if (!g_up)
            return SCM_BOOL_F;
        const auto n = hl::notificationOf(id);
        return n ? scm_from_double(n->timeMs()) : SCM_BOOL_F;
    }

    static SCM hlNotificationColor(SCM id) {
        if (!g_up)
            return SCM_BOOL_F;
        const auto n = hl::notificationOf(id);
        return n ? scm_from_int64(n->color().getAsHex()) : SCM_BOOL_F;
    }

    static SCM hlNotificationIcon(SCM id) {
        if (!g_up)
            return SCM_BOOL_F;
        const auto n = hl::notificationOf(id);
        return n ? scm_from_int64(sc<int>(n->icon())) : SCM_BOOL_F;
    }

    static SCM hlNotificationFontSize(SCM id) {
        if (!g_up)
            return SCM_BOOL_F;
        const auto n = hl::notificationOf(id);
        return n ? scm_from_double(n->fontSize()) : SCM_BOOL_F;
    }

    static SCM hlNotificationElapsed(SCM id) {
        if (!g_up)
            return SCM_BOOL_F;
        const auto n = hl::notificationOf(id);
        return n ? scm_from_double(n->timeElapsedMs()) : SCM_BOOL_F;
    }

    static SCM hlNotificationAge(SCM id) {
        if (!g_up)
            return SCM_BOOL_F;
        const auto n = hl::notificationOf(id);
        return n ? scm_from_double(n->timeElapsedSinceCreationMs()) : SCM_BOOL_F;
    }

    static SCM hlNotificationAlive(SCM id) {
        if (!g_up)
            return SCM_BOOL_F;
        return hl::notificationOf(id) ? SCM_BOOL_T : SCM_BOOL_F;
    }

    static SCM hlNotificationSame(SCM a, SCM b) {
        if (!g_up)
            return SCM_BOOL_F;
        const auto na = hl::notificationOf(a);
        const auto nb = hl::notificationOf(b);
        return (na && nb && na.get() == nb.get()) ? SCM_BOOL_T : SCM_BOOL_F;
    }

    // expired handles mutate as silent no-ops (upstream parity)
    static int hlNotificationTextSet(SCM id, const char* text) {
        if (!g_up)
            return -1;
        if (const auto n = hl::notificationOf(id))
            n->setText(std::string(text ? text : ""));
        return 0;
    }

    static int hlNotificationTimeoutSet(SCM id, double ms) {
        if (!g_up)
            return -1;
        if (ms < 0) {
            g_configError = "hl-notification-timeout-set!: timeout must be >= 0";
            return -2;
        }
        if (const auto n = hl::notificationOf(id))
            n->resetTimeout(sc<float>(ms));
        return 0;
    }

    static int hlNotificationColorSet(SCM id, SCM color) {
        if (!g_up)
            return -1;
        const auto c = colorFromNumber(color);
        if (!c) {
            g_configError = "hl-notification-color-set!: bad color (expected a 0xAARRGGBB integer)";
            return -2;
        }
        if (const auto n = hl::notificationOf(id))
            n->setColor(*c);
        return 0;
    }

    // string->color: the deliberate escape hatch for a colour written the way the
    // CONFIG FILE spells it. Takes one of Hyprland's colour strings and answers
    // the 0xAARRGGBB integer every colour argument here wants. #f when the
    // compositor's parser refuses it.
    static SCM hlStringColor(SCM s) {
        if (!scm_is_string(s))
            return SCM_BOOL_F;
        const auto c = colorFromString(schemeDatumToStr(s));
        return c ? scm_from_uint64(c->getAsHex()) : SCM_BOOL_F;
    }

    static int hlNotificationIconSet(SCM id, SCM v) {
        if (!g_up)
            return -1;
        bool ok = true;
        const auto icon = schemeIconFromScheme(v, &ok);
        if (!ok) {
            g_configError = "hl-notification-icon-set!: bad icon (expected none/warn/info/hint/error/confused/ok or an id 0-6)";
            return -2;
        }
        if (const auto n = hl::notificationOf(id))
            n->setIcon(icon);
        return 0;
    }

    static int hlNotificationFontSizeSet(SCM id, double size) {
        if (!g_up)
            return -1;
        if (size <= 0) {
            g_configError = "hl-notification-font-size-set!: font size must be > 0";
            return -2;
        }
        if (const auto n = hl::notificationOf(id))
            n->setFontSize(sc<float>(size));
        return 0;
    }

    static int hlNotificationPausedSet(SCM id, bool on) {
        if (!g_up)
            return -1;
        auto* h = hl::notificationHandleOf(id);
        if (const auto n = h->wp.lock()) {
            if (on && !h->paused) {
                n->lock();
                h->paused = true;
            } else if (!on && h->paused) {
                n->unlock();
                h->paused = false;
            }
        }
        return 0;
    }

    static SCM hlNotificationPausedQ(SCM id) {
        if (!g_up)
            return SCM_BOOL_F;
        const auto n = hl::notificationOf(id);
        if (!n)
            return SCM_BOOL_F;
        return n->isLocked() ? SCM_BOOL_T : SCM_BOOL_F;
    }

    static int hlNotificationDismiss(SCM id) {
        if (!g_up)
            return -1;
        if (const auto n = hl::notificationOf(id))
            Notification::overlay()->dismissNotification(n);
        return 0;
    }

    // this family's Scheme-visible surface
    void registerNotification() {
        hl::bind<hlNotify>("hl--c-notify");
        hl::bind<hlNotificationAdd>("hl--c-notification-add");
        hl::bind<hlNotificationList>("hl--c-notification-list");
        hl::bind<hlNotificationText>("hl--c-notification-text");
        hl::bind<hlNotificationTimeout>("hl--c-notification-timeout");
        hl::bind<hlNotificationColor>("hl--c-notification-color");
        hl::bind<hlNotificationIcon>("hl--c-notification-icon");
        hl::bind<hlNotificationFontSize>("hl--c-notification-font-size");
        hl::bind<hlNotificationElapsed>("hl--c-notification-elapsed");
        hl::bind<hlNotificationAge>("hl--c-notification-age");
        hl::bind<hlNotificationAlive>("hl--c-notification-alive");
        hl::bind<hlNotificationSame>("hl--c-notification-same");
        hl::bind<hlNotificationTextSet>("hl--c-notification-text-set");
        hl::bind<hlNotificationTimeoutSet>("hl--c-notification-timeout-set");
        hl::bind<hlNotificationColorSet>("hl--c-notification-color-set");
        hl::bind<hlStringColor>("hl--c-string-color");
        hl::bind<hlNotificationIconSet>("hl--c-notification-icon-set");
        hl::bind<hlNotificationFontSizeSet>("hl--c-notification-font-size-set");
        hl::bind<hlNotificationPausedSet>("hl--c-notification-paused-set");
        hl::bind<hlNotificationPausedQ>("hl--c-notification-paused-q");
        hl::bind<hlNotificationDismiss>("hl--c-notification-dismiss");
    }

} // namespace Config::Scheme

#include "SchemeManager.hpp"
#include "SchemeLayout.hpp"

#include <src/debug/log/Logger.hpp>
#include <src/debug/crash/CrashReporter.hpp>
#include <src/event/EventBus.hpp>
#include <src/helpers/memory/Memory.hpp>
#include <src/keybinds/Manager.hpp>
// g_pEventLoopManager is an inline variable: each DSO gets its own copy
// unless references can preempt to the executable's exported (GNU_UNIQUE)
// instance. -fvisibility=hidden would bind us to a private, forever-null
// copy — the loop manager is only reachable through unification.
#pragma GCC visibility push(default)
#include <src/managers/eventLoop/EventLoopManager.hpp>
#pragma GCC visibility pop
#include <src/managers/eventLoop/EventLoopTimer.hpp>
#include <src/desktop/state/FocusState.hpp>
#include <src/desktop/state/WindowState.hpp>
#include <src/desktop/view/window/Window.hpp>
#include <src/managers/fullscreen/FullscreenController.hpp>
#include <src/managers/fullscreen/FullscreenTypes.hpp>
#include <src/pointer/PointerManager.hpp>
#include <src/state/MonitorState.hpp>
#include <src/state/WorkspaceState.hpp>
#include <src/state/workspace/Resolver.hpp>
#include <src/workspace/HLWorkspace.hpp>
#include <src/config/shared/actions/ConfigActions.hpp>
#include <src/helpers/math/Direction.hpp>
#include <src/input/Keys.hpp>
#include <src/config/ConfigValue.hpp>
#include <src/ipc/s1/S1.hpp>

#include <unordered_map>
#include <src/config/supplementary/executor/Executor.hpp>

#include <hyprutils/os/FileDescriptor.hpp>

#include <sys/inotify.h>
#include <fcntl.h>
#include <unistd.h>

#include <cstring>
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>


extern "C" {
#include <scheme.h>
}


using namespace Hyprutils::OS;
using Hyprutils::OS::CFileDescriptor;

/*
    Bootstrap, evaluated by the interpreter before the user config is loaded.

    Loaded in two phases:
      1. SCHEME_PRELUDE is loaded unguarded. It only contains the error
         plumbing (verified, static); an error here means the embedded
         error handler exits — the same failure mode as before.
      2. SCHEME_BOOTSTRAP is loaded *through* the prelude's guarded hl--load.
         Any error prints and leaves hl--ready #f; the C side then disables
         scheme instead of continuing half-initialized.

    After a successful bootstrap the scripting API is:

        (hl-bind "SUPER SHIFT" "T" (lambda () ...))   -> id | error
        (hl-bind mods key thunk 'release #t 'description "d")  -> id | error
            options: release repeat locked non-consuming long-press
                     ignore-mods transparent description
        (hl-exec "command")                           -> pid | -1
        (hl-after ms (lambda () ...))                 -> id | error   (one-shot)
        (hl-repeat ms (lambda () ...))                -> id | error   (until reload;
                                                        a callback error stops it)
        (hl-active-title)                             -> string | #f
        (hl-workspaces)                               -> list of strings
        (hl-on-submap (lambda (name) ...))            -> id | error   (until reload)
        (hl-active-window)                            -> window | #f
        (hl-windows)                                  -> list of windows
        (hl-window-title w)                           -> string | #f  (| #f = stale)
        (hl-window-alive? w)                          -> bool
        (hl-window-close w)                           -> bool
        (hl-window-class w)                           -> string | #f
        (hl-window-workspace w)                       -> string | #f
        (hl-window-monitor w)                         -> string | #f
        (hl-window-floating? w)                       -> bool
        (hl-window-size w)                            -> (w . h) | #f
        (hl-window-pid w)                             -> int | -1
        (hl-window-focus w) / (hl-window-float w)     -> bool
        (hl-window-move-to-workspace w "name")        -> bool (creates missing
                                                        workspaces on w's monitor)
        (hl-window-fullscreen w) / (hl-window-maximize w) -> bool (toggles)
        (hl-window-fullscreen-mode w)                 -> 0 none | 1 max | 2 full
        (hl-window-hidden? w) / (hl-window-pinned? w) / (hl-window-x11? w) -> bool
        (hl-window-initial-class w) / (hl-window-initial-title w) -> string | #f
        (hl-monitors)                                 -> list of names
        (hl-on-window-open (lambda (w) ...))          -> id | error   (w = handle)
        (hl-on-window-close (lambda (w) ...))         -> id | error   (w = handle)
        (hl-on-workspace-active (lambda (name) ...))  -> id | error
        (hl-window=? a b)                             -> bool (same window)
        (hl-unbind id)                                -> bool
        (hl-current-submap)                           -> string
        (hl-cursor-pos)                               -> (x . y) | #f
        (hl-define-layout "name" (lambda (count W H windows) ...))
                                                      -> id | error
            pure-function layout: returns one (x y w h) list per window;
            windows[i] is the handle for box i (queries on it work during
            the callback). coordinates within the work area. selected via
            `layout = scheme:name`. on error -> default grid for the
            generation. WARNING: no watchdog — an infinite loop freezes.
        (hl-submap "name" (lambda () ...binds...))    -> submap scope
        (hl-enter-submap "name") / (hl-exit-submap)   -> bool (switch active)
        (hl-state-set! 'key value)                    -> value
        (hl-state-ref 'key [default])                 -> value | default | #f
        (hl-state-keys)                               -> list of keys
*/

static constexpr const char* SCHEME_PRELUDE = R"scm(
;; the default embedded error handler calls exit on uncaught exceptions,
;; so everything user- or config-supplied must run under these guards.
(define hl--ready #f)

(define (hl--report e)
  (display "[scheme] error: " (current-error-port))
  (display-condition e (current-error-port))
  (newline (current-error-port))
  #f)

(define (hl--fire id)
  (let ((entry (assv id hl--binds)))
    (if (not entry)
        #f
        (guard (e (#t (hl--report e)))
          ((cdr entry))
          #t))))

(define (hl--fire-str id arg)
  (let ((entry (assv id hl--binds)))
    (if (not entry)
        #f
        (guard (e (#t (hl--report e)))
          ((cdr entry) arg)
          #t))))

;; defined here so event handlers receive real window records; the record
;; constructor lives in the bootstrap, loaded after this prelude
(define (hl--fire-win id win-id)
  (let ((entry (assv id hl--binds)))
    (if (not entry)
        #f
        (guard (e (#t (hl--report e)))
          ((cdr entry) (make-hl-window win-id))
          #t))))

;; events carrying (window, bool) payloads: minimize state
(define (hl--fire-win-state id win-id state)
  (let ((entry (assv id hl--binds)))
    (if (not entry)
        #f
        (guard (e (#t (hl--report e)))
          ((cdr entry) (make-hl-window win-id) state)
          #t))))

(define (hl--load path)
  (guard (e (#t (hl--report e)))
    (load path)
    #t))

;; hyprctl scheme entry: evaluate all forms in the string, reply with the
;; last value formatted, or the error text
(define (hl--eval code)
  (guard (e (#t (call-with-string-output-port
                  (lambda (p)
                    (display "error: " p)
                    (display-condition e p)))))
    (let loop ((port (open-input-string code)) (result (void)))
      (let ((form (read port)))
        (if (eof-object? form)
            (format "~s" result)
            (loop port (eval form)))))))

;; layout callbacks: spec = "count\nW\nH\n<handle-id per target>"
;; (kept to 2 foreign args; Scall passes at most 3). the fn receives
;; (count W H windows) where windows[i] is the handle for box i (a handle
;; whose id 0 means "not a window" — every query on it returns #f).
;; fn returns list of (x y w h), or #f on error.
(define (hl--split-lines s)
  (let loop ((i 0) (start 0) (acc '()))
    (cond
      ((>= i (string-length s))
       (reverse (cons (substring s start i) acc)))
      ((char=? (string-ref s i) #\newline)
       (loop (+ i 1) (+ i 1) (cons (substring s start i) acc)))
      (else (loop (+ i 1) start acc)))))

;; layout event dispatch. a layout provider is an alist of callbacks:
;;   ((recalculate . fn) (resize . fn) (window-open . fn) (window-close . fn))
;; recalculate/resize fn: (count W H windows [dx dy corner]) -> ((x y w h) ...)
;; window callbacks: (window) -> ignored. #f when absent or on error.
(define (hl--layout-event id event payload)
  (let* ((prov (assv id hl--binds))
         (cbs  (if prov (cdr prov) #f)))
    (if (not cbs)
        #f
        (guard (e (#t (hl--report e)))
          (cond
            ((equal? event "window-open")
             (let ((cb (assq 'window-open cbs)))
               (if cb ((cdr cb) (make-hl-window (string->number payload))) #f)))
            ((equal? event "window-close")
             (let ((cb (assq 'window-close cbs)))
               (if cb ((cdr cb) (make-hl-window (string->number payload))) #f)))
            ((equal? event "layout-msg")
             (let ((cb (assq 'layout-msg cbs)))
               (if (not cb)
                   ""
                   (let ((r ((cdr cb) payload)))
                     (cond ((not r) "rejected")
                           ((string? r) r)
                           (else ""))))))
            (else
             (let* ((parts (hl--split-lines payload))
                    (count (string->number (list-ref parts 0)))
                    (W     (string->number (list-ref parts 1)))
                    (H     (string->number (list-ref parts 2))))
               (if (equal? event "resize")
                   (let* ((dx     (string->number (list-ref parts 3)))
                          (dy     (string->number (list-ref parts 4)))
                          (corner (string->number (list-ref parts 5)))
                          (wins   (map make-hl-window (map string->number (list-tail parts 6))))
                          (cb     (or (assq 'resize cbs) (assq 'recalculate cbs))))
                     (if cb (apply (cdr cb) (list count W H wins dx dy corner)) #f))
                   (let ((wins (map make-hl-window (map string->number (list-tail parts 3))))
                         (cb   (assq 'recalculate cbs)))
                     (if cb ((cdr cb) count W H wins) #f))))))))))
)scm";

static constexpr const char* SCHEME_BOOTSTRAP = R"scm(
(define hl--binds '())

(define c-hl-bind (foreign-procedure "hl-scheme-bind" (string string int string string) int))
(define c-hl-exec (foreign-procedure "hl-scheme-exec" (string) int))
(define c-hl-timer (foreign-procedure "hl-scheme-timer" (int int) int))
(define c-hl-active-title (foreign-procedure "hl-scheme-active-title" () scheme-object))
(define c-hl-workspace-names (foreign-procedure "hl-scheme-workspace-names" () scheme-object))
(define c-hl-submap-listen (foreign-procedure "hl-scheme-submap-listen" () int))
(define c-hl-active-window-id (foreign-procedure "hl-scheme-active-window-id" () int))
(define c-hl-window-ids (foreign-procedure "hl-scheme-window-ids" () scheme-object))
(define c-hl-window-title (foreign-procedure "hl-scheme-window-title" (int) scheme-object))
(define c-hl-window-alive (foreign-procedure "hl-scheme-window-alive" (int) int))
(define c-hl-window-close (foreign-procedure "hl-scheme-window-close" (int) int))
(define c-hl-window-class (foreign-procedure "hl-scheme-window-class" (int) scheme-object))
(define c-hl-window-workspace-name (foreign-procedure "hl-scheme-window-workspace-name" (int) scheme-object))
(define c-hl-window-monitor-name (foreign-procedure "hl-scheme-window-monitor-name" (int) scheme-object))
(define c-hl-window-floating (foreign-procedure "hl-scheme-window-floating" (int) int))
(define c-hl-window-size (foreign-procedure "hl-scheme-window-size" (int) scheme-object))
(define c-hl-window-pid (foreign-procedure "hl-scheme-window-pid" (int) int))
(define c-hl-window-focus (foreign-procedure "hl-scheme-window-focus" (int) int))
(define c-hl-window-float (foreign-procedure "hl-scheme-window-float" (int) int))
(define c-hl-window-move-to-workspace (foreign-procedure "hl-scheme-window-move-to-workspace" (int string) int))
(define c-hl-monitor-names (foreign-procedure "hl-scheme-monitor-names" () scheme-object))
(define c-hl-window-event-listen (foreign-procedure "hl-scheme-window-event-listen" (int) int))
(define c-hl-window-minimize-listen (foreign-procedure "hl-scheme-window-minimize-listen" () int))
(define c-hl-lifecycle-listen (foreign-procedure "hl-scheme-lifecycle-listen" (int) int))
(define c-hl-config-reloaded-listen (foreign-procedure "hl-scheme-config-reloaded-listen" () int))
(define c-hl-unbind (foreign-procedure "hl-scheme-unbind" (int) int))
(define c-hl-window-same (foreign-procedure "hl-scheme-window-same" (int int) int))
(define c-hl-current-submap (foreign-procedure "hl-scheme-current-submap" () scheme-object))
(define c-hl-cursor-pos (foreign-procedure "hl-scheme-cursor-pos" () scheme-object))
(define c-hl-workspace-active-listen (foreign-procedure "hl-scheme-workspace-active-listen" () int))
(define c-hl-define-layout (foreign-procedure "hl-scheme-define-layout" (string) int))

;; pure-function layout; see SchemeLayout.hpp for the contract
;; spec is a single recalculate fn, or an alist of callbacks:
;;   ((recalculate . fn) (resize . fn) (window-open . fn) (window-close . fn))
;; resize fn: (count W H windows dx dy corner) -> boxes; when absent a resize
;; falls back to the recalculate fn. state: keep it in a closure around fn.
(define (hl-define-layout name spec)
  (let* ((prov (if (procedure? spec) (list (cons 'recalculate spec)) spec))
         (id   (c-hl-define-layout name)))
    (if (< id 0)
        (errorf 'hl-define-layout "layout ~a rejected, see compositor log" name)
        (hl--register id prov))))

(define c-hl-get-submap-ctx (foreign-procedure "hl-scheme-get-submap-ctx" () scheme-object))
(define c-hl-set-submap-ctx (foreign-procedure "hl-scheme-set-submap-ctx" (string string) void))
(define c-hl-enter-submap (foreign-procedure "hl-scheme-enter-submap" (string) int))

;; binds registered inside fn are scoped to the submap; the optional reset
;; names the submap returned to on exit. the submap exists only if fn
;; registers at least one bind.
(define (hl-submap name fn . reset)
  (let* ((ctx        (hl--split-lines (c-hl-get-submap-ctx)))
         (prev-name  (list-ref ctx 0))
         (prev-reset (list-ref ctx 1)))
    (dynamic-wind
      (lambda () (c-hl-set-submap-ctx name (if (null? reset) "" (car reset))))
      (lambda () (fn))
      (lambda () (c-hl-set-submap-ctx prev-name prev-reset)))))

;; switch the active submap ("" or "reset" returns to the default)
(define (hl-enter-submap name)
  (= 0 (c-hl-enter-submap name)))

(define (hl-exit-submap)
  (= 0 (c-hl-enter-submap "")))
(define c-hl-window-fullscreen-toggle (foreign-procedure "hl-scheme-window-fullscreen-toggle" (int int) int))
(define c-hl-window-fullscreen-mode (foreign-procedure "hl-scheme-window-fullscreen-mode" (int) int))
(define c-hl-window-hidden (foreign-procedure "hl-scheme-window-hidden" (int) int))
(define c-hl-window-pinned (foreign-procedure "hl-scheme-window-pinned" (int) int))
(define c-hl-window-initial-class (foreign-procedure "hl-scheme-window-initial-class" (int) scheme-object))
(define c-hl-window-initial-title (foreign-procedure "hl-scheme-window-initial-title" (int) scheme-object))

;; ---- actions: the dispatcher surface (window id -1 = active window) --------
(define c-hl-focus-workspace (foreign-procedure "hl-scheme-focus-workspace" (string) int))
(define c-hl-focus-direction (foreign-procedure "hl-scheme-focus-direction" (string) int))
(define c-hl-focus-monitor (foreign-procedure "hl-scheme-focus-monitor" (string) int))
(define c-hl-focus-last (foreign-procedure "hl-scheme-focus-last" () int))
(define c-hl-focus-urgent (foreign-procedure "hl-scheme-focus-urgent" () int))
(define c-hl-window-move-direction (foreign-procedure "hl-scheme-window-move-direction" (int string) int))
(define c-hl-window-swap-direction (foreign-procedure "hl-scheme-window-swap-direction" (int string) int))
(define c-hl-window-swap-next (foreign-procedure "hl-scheme-window-swap-next" (int int) int))
(define c-hl-window-swap-with (foreign-procedure "hl-scheme-window-swap-with" (int int) int))
(define c-hl-window-float-act (foreign-procedure "hl-scheme-window-float-act" (int int) int))
(define c-hl-window-cycle (foreign-procedure "hl-scheme-window-cycle" (int int int) int))
(define c-hl-window-center (foreign-procedure "hl-scheme-window-center" (int) int))
(define c-hl-window-resize-px (foreign-procedure "hl-scheme-window-resize-px" (int double double int) int))
(define c-hl-window-move-px (foreign-procedure "hl-scheme-window-move-px" (int double double int) int))
(define c-hl-window-pin-act (foreign-procedure "hl-scheme-window-pin-act" (int int) int))
(define c-hl-window-pseudo (foreign-procedure "hl-scheme-window-pseudo" (int int) int))
(define c-hl-window-kill (foreign-procedure "hl-scheme-window-kill" (int) int))
(define c-hl-window-signal (foreign-procedure "hl-scheme-window-signal" (int int) int))
(define c-hl-window-zorder (foreign-procedure "hl-scheme-window-zorder" (int string) int))
(define c-hl-window-set-prop (foreign-procedure "hl-scheme-window-set-prop" (int string string) int))
(define c-hl-window-tag (foreign-procedure "hl-scheme-window-tag" (int string) int))
(define c-hl-window-clear-tags (foreign-procedure "hl-scheme-window-clear-tags" (int) int))
(define c-hl-toggle-swallow (foreign-procedure "hl-scheme-toggle-swallow" () int))
(define c-hl-group-toggle (foreign-procedure "hl-scheme-group-toggle" (int) int))
(define c-hl-group-cycle (foreign-procedure "hl-scheme-group-cycle" (int int) int))
(define c-hl-group-index (foreign-procedure "hl-scheme-group-index" (int int) int))
(define c-hl-group-move-window (foreign-procedure "hl-scheme-group-move-window" (int int) int))
(define c-hl-group-lock (foreign-procedure "hl-scheme-group-lock" (int) int))
(define c-hl-group-lock-active (foreign-procedure "hl-scheme-group-lock-active" (int) int))
(define c-hl-window-into-group (foreign-procedure "hl-scheme-window-into-group" (int string) int))
(define c-hl-window-out-of-group (foreign-procedure "hl-scheme-window-out-of-group" (int string) int))
(define c-hl-window-into-or-create-group (foreign-procedure "hl-scheme-window-into-or-create-group" (int string) int))
(define c-hl-window-deny-from-group (foreign-procedure "hl-scheme-window-deny-from-group" (int int) int))
(define c-hl-workspace-rename (foreign-procedure "hl-scheme-workspace-rename" (string string) int))
(define c-hl-workspace-move-monitor (foreign-procedure "hl-scheme-workspace-move-monitor" (string string) int))
(define c-hl-workspace-toggle-special (foreign-procedure "hl-scheme-workspace-toggle-special" (string) int))
(define c-hl-workspace-swap-monitors (foreign-procedure "hl-scheme-workspace-swap-monitors" (string string) int))
(define c-hl-cursor-move (foreign-procedure "hl-scheme-cursor-move" (double double) int))
(define c-hl-cursor-corner (foreign-procedure "hl-scheme-cursor-corner" (int int) int))
(define c-hl-exit (foreign-procedure "hl-scheme-exit" () int))
(define c-hl-reload-config (foreign-procedure "hl-scheme-reload-config" () int))
(define c-hl-force-renderer-reload (foreign-procedure "hl-scheme-force-renderer-reload" () int))
(define c-hl-dpms (foreign-procedure "hl-scheme-dpms" (int string) int))
(define c-hl-force-idle (foreign-procedure "hl-scheme-force-idle" (double) int))
(define c-hl-global (foreign-procedure "hl-scheme-global" (string) int))
(define c-hl-event (foreign-procedure "hl-scheme-event" (string) int))
(define c-hl-pass (foreign-procedure "hl-scheme-pass" (int) int))
(define c-hl-send-shortcut (foreign-procedure "hl-scheme-send-shortcut" (int int int) int))
(define c-hl-send-key-state (foreign-procedure "hl-scheme-send-key-state" (int int int int) int))
(define c-hl-mouse (foreign-procedure "hl-scheme-mouse" (string) int))
(define c-hl-release-input-capture (foreign-procedure "hl-scheme-release-input-capture" () int))
(define c-hl-window-fullscreen-state (foreign-procedure "hl-scheme-window-fullscreen-state" (int int int int) int))
(define c-hl-layout-message (foreign-procedure "hl-scheme-layout-message" (string) int))

;; helpers for the action wrappers: window #f = active; actions 'toggle/'on/'off;
;; directions "l"/"r"/"u"/"d" or the symbols left/right/up/down
(define (hl--wid w)
  (if w (hl-window-id w) -1))

(define (hl--togact a)
  (case a
    ((toggle) 0)
    ((on enable) 1)
    ((off disable) 2)
    ((#f) 0)
    (else (if (number? a) a 0))))

(define (hl--dir d)
  (if (symbol? d) (symbol->string d) d))

;; names/tags/props accept symbols or strings (numbers become strings)
(define (hl--str s)
  (cond ((symbol? s) (symbol->string s))
        ((number? s) (number->string s))
        (else s)))
(define c-hl-window-x11 (foreign-procedure "hl-scheme-window-x11" (int) int))

(define (hl--register id thunk)
  (set! hl--binds (cons (cons id thunk) hl--binds))
  id)

;; flat option list -> assoc list: ('release #t 'description "x")
(define (hl--pairs l)
  (cond
    ((null? l) '())
    ((null? (cdr l)) (errorf 'hl--pairs "odd option list"))
    (else (cons (cons (car l) (cadr l)) (hl--pairs (cddr l))))))

(define (hl--opt alist key)
  (let ((entry (assq key alist)))
    (if entry (cdr entry) #f)))

;; eBindFlags bits from src/keybinds/Bind.hpp
(define (hl--bind-flags alist)
  (let ((click (hl--opt alist 'click))
        (drag  (hl--opt alist 'drag)))
    (when (and click drag)
      (errorf 'hl-bind "click and drag are exclusive"))
    (when (and (hl--opt alist 'mouse)
               (or (hl--opt alist 'repeat) (hl--opt alist 'locked) (hl--opt alist 'release)))
      (errorf 'hl-bind "mouse is exclusive with repeat/locked/release"))
    (+ (if (hl--opt alist 'release) 2 0)
       (if (hl--opt alist 'repeat) 4 0)
       (if (hl--opt alist 'locked) 1 0)
       (if (hl--opt alist 'non-consuming) 16 0)
       (if (hl--opt alist 'long-press) 8 0)
       (if (hl--opt alist 'transparent) 64 0)
       (if (hl--opt alist 'ignore-mods) 128 0)
       (if (hl--opt alist 'mouse) 32768 0)
       (if (or click drag) 2 0)              ; click/drag imply release
       (if click 512 0)
       (if drag 1024 0)
       (if (hl--opt alist 'device-inclusive) 8192 0))))

(define (hl-bind mods key thunk . opts)
  (let* ((alist (hl--pairs opts))
         (id (c-hl-bind mods key
                         (+ (hl--bind-flags alist)
                            (if (equal? key "catchall") 16384 0))
                         (or (hl--opt alist 'description) "")
                         (let ((ds (hl--opt alist 'devices)))
                           (if (list? ds)
                               (let loop ((rest ds) (acc ""))
                                 (cond ((null? rest) acc)
                                       ((null? (cdr rest)) (string-append acc (car rest)))
                                       (else (loop (cdr rest) (string-append acc (car rest) ",")))))
                               "")))))
    (if (< id 0)
        (errorf 'hl-bind "bind ~a ~a rejected, see compositor log" mods key)
        (hl--register id thunk))))

(define (hl-exec cmd)
  (c-hl-exec cmd))

(define (hl-after ms thunk)
  (let ((id (c-hl-timer ms 0)))
    (if (< id 0)
        (errorf 'hl-after "timer ~ams rejected, see compositor log" ms)
        (hl--register id thunk))))

(define (hl-repeat ms thunk)
  (let ((id (c-hl-timer ms 1)))
    (if (< id 0)
        (errorf 'hl-repeat "timer ~ams rejected, see compositor log" ms)
        (hl--register id thunk))))

;; returns #f when no window has focus
(define (hl-active-title)
  (c-hl-active-title))

;; collections are marshalled as one newline-joined string and split here
;; (hl--split-lines lives in the prelude), so all list building happens
;; under Scheme's GC (no raw ptrs across calls)
(define (hl-workspaces)
  (let ((joined (c-hl-workspace-names)))
    (if (eq? joined #f)
        '()
        (hl--split-lines joined))))

(define (hl-on-submap thunk)
  (let ((id (c-hl-submap-listen)))
    (if (< id 0)
        (errorf 'hl-on-submap "listener rejected, see compositor log")
        (hl--register id thunk))))

;; windows are opaque records wrapping an int id. C++ keeps a weak reference
;; keyed by id; the compositor can destroy the window at any moment, and
;; stale handles simply report #f — the id is the only thing crossing FFI.
(define-record-type hl-window (fields id))

(define (hl-active-window)
  (let ((id (c-hl-active-window-id)))
    (if (< id 0) #f (make-hl-window id))))

(define (hl-windows)
  (let ((joined (c-hl-window-ids)))
    (if (eq? joined #f)
        '()
        (map make-hl-window (map string->number (hl--split-lines joined))))))

(define (hl-window-title w)
  (c-hl-window-title (hl-window-id w)))

(define (hl-window-alive? w)
  (= 1 (c-hl-window-alive (hl-window-id w))))

(define (hl-window-close w)
  (= 0 (c-hl-window-close (hl-window-id w))))

(define (hl-window-class w)
  (c-hl-window-class (hl-window-id w)))

(define (hl-window-workspace w)
  (c-hl-window-workspace-name (hl-window-id w)))

(define (hl-window-monitor w)
  (c-hl-window-monitor-name (hl-window-id w)))

(define (hl-window-floating? w)
  (= 1 (c-hl-window-floating (hl-window-id w))))

;; => (width . height), or #f when stale
(define (hl-window-size w)
  (let ((s (c-hl-window-size (hl-window-id w))))
    (if (eq? s #f)
        #f
        (let ((wh (map string->number (hl--split-lines s))))
          (cons (car wh) (cadr wh))))))

(define (hl-window-pid w)
  (c-hl-window-pid (hl-window-id w)))

(define (hl-window-focus w)
  (= 0 (c-hl-window-focus (hl-window-id w))))

;; act: 'toggle (default), 'on, 'off
(define (hl-window-float w . opt)
  (= 0 (c-hl-window-float-act (hl--wid w) (hl--togact (if (null? opt) 'toggle (car opt))))))

(define (hl-window-move-to-workspace w name)
  (= 0 (c-hl-window-move-to-workspace (hl--wid w) (hl--str name))))

;; ---- actions: navigation and geometry --------------------------------------
;; directions: "l"/"r"/"u"/"d" or 'left/'right/'up/'down. Window args accept
;; a handle or #f (= active window). Action results: #t on success, #f on
;; rejection (message in the compositor log).

(define (hl-focus-workspace name)
  (= 0 (c-hl-focus-workspace (hl--str name))))

(define (hl-focus-direction dir)
  (= 0 (c-hl-focus-direction (hl--dir dir))))

(define (hl-focus-monitor name)
  (= 0 (c-hl-focus-monitor (hl--str name))))

(define (hl-focus-last)
  (= 0 (c-hl-focus-last)))

(define (hl-focus-urgent)
  (= 0 (c-hl-focus-urgent)))

(define (hl-window-move-dir w dir)
  (= 0 (c-hl-window-move-direction (hl--wid w) (hl--dir dir))))

(define (hl-window-swap-dir w dir)
  (= 0 (c-hl-window-swap-direction (hl--wid w) (hl--dir dir))))

;; prev: 'prev or #t swaps backwards
(define (hl-window-swap-next w . opt)
  (= 0 (c-hl-window-swap-next (hl--wid w) (if (null? opt) 0 (if (eq? (car opt) 'prev) 1 (if (car opt) 1 0))))))

(define (hl-window-swap-with w other)
  (= 0 (c-hl-window-swap-with (hl--wid w) (hl-window-id other))))

;; opts: 'prev, 'tiled, 'floating (combinable symbols)
(define (hl-window-cycle . opt)
  (let loop ((rest opt) (next 1) (filter 0))
    (cond ((null? rest)
           (= 0 (c-hl-window-cycle -1 next filter)))
          ((eq? (car rest) 'prev) (loop (cdr rest) 0 filter))
          ((eq? (car rest) 'tiled) (loop (cdr rest) next 1))
          ((eq? (car rest) 'floating) (loop (cdr rest) next 2))
          (else (loop (cdr rest) next filter)))))

(define (hl-window-center w)
  (= 0 (c-hl-window-center (hl--wid w))))

;; absolute by default; 'relative (or 'rel) makes the deltas relative
(define (hl-window-resize w width height . opt)
  (= 0 (c-hl-window-resize-px (hl--wid w) (exact->inexact width) (exact->inexact height)
         (if (null? opt) 0 (if (memq (car opt) '(relative rel)) 1 0)))))

(define (hl-window-move-px w x y . opt)
  (= 0 (c-hl-window-move-px (hl--wid w) (exact->inexact x) (exact->inexact y)
         (if (null? opt) 0 (if (memq (car opt) '(relative rel)) 1 0)))))

(define (hl-window-pin w . opt)
  (= 0 (c-hl-window-pin-act (hl--wid w) (hl--togact (if (null? opt) 'toggle (car opt))))))

(define (hl-window-pseudo w . opt)
  (= 0 (c-hl-window-pseudo (hl--wid w) (hl--togact (if (null? opt) 'toggle (car opt))))))

(define (hl-window-kill w)
  (= 0 (c-hl-window-kill (hl--wid w))))

(define (hl-window-signal w sig)
  (= 0 (c-hl-window-signal (hl--wid w) sig)))

;; mode: "up" | "down" | "top" | "bottom" (alterZOrder mode string)
(define (hl-window-zorder w mode)
  (= 0 (c-hl-window-zorder (hl--wid w) (hl--str mode))))

(define (hl-window-set-prop w prop val)
  (= 0 (c-hl-window-set-prop (hl--wid w) (hl--str prop) (hl--str val))))

(define (hl-window-tag w tag)
  (= 0 (c-hl-window-tag (hl--wid w) (hl--str tag))))

(define (hl-window-clear-tags w)
  (= 0 (c-hl-window-clear-tags (hl--wid w))))

(define (hl-window-toggle-swallow)
  (= 0 (c-hl-toggle-swallow)))

;; ---- actions: groups --------------------------------------------------------

(define (hl-group-toggle w)
  (= 0 (c-hl-group-toggle (hl--wid w))))

(define (hl-group-cycle w . opt)
  (= 0 (c-hl-group-cycle (hl--wid w) (if (null? opt) 0 (if (eq? (car opt) 'prev) 1 0)))))

(define (hl-group-index w index)
  (= 0 (c-hl-group-index (hl--wid w) index)))

(define (hl-group-move-window w . opt)
  (= 0 (c-hl-group-move-window (hl--wid w) (if (null? opt) 0 (if (eq? (car opt) 'prev) 1 0)))))

;; act: 'toggle/'on/'off
(define (hl-group-lock act)
  (= 0 (c-hl-group-lock (hl--togact act))))

(define (hl-group-lock-active act)
  (= 0 (c-hl-group-lock-active (hl--togact act))))

(define (hl-window-into-group w dir)
  (= 0 (c-hl-window-into-group (hl--wid w) (hl--dir dir))))

(define (hl-window-out-of-group w dir)
  (= 0 (c-hl-window-out-of-group (hl--wid w) (hl--dir dir))))

(define (hl-window-into-or-create-group w dir)
  (= 0 (c-hl-window-into-or-create-group (hl--wid w) (hl--dir dir))))

(define (hl-window-deny-from-group w . opt)
  (= 0 (c-hl-window-deny-from-group (hl--wid w)
         (hl--togact (if (null? opt) 'toggle (car opt))))))

;; ---- actions: workspaces and monitors ---------------------------------------

(define (hl-workspace-rename old-name new-name)
  (= 0 (c-hl-workspace-rename (hl--str old-name) (hl--str new-name))))

(define (hl-workspace-move-to-monitor ws mon)
  (= 0 (c-hl-workspace-move-monitor (hl--str ws) (hl--str mon))))

(define (hl-workspace-toggle-special name)
  (= 0 (c-hl-workspace-toggle-special (hl--str name))))

(define (hl-workspace-swap-monitors mon1 mon2)
  (= 0 (c-hl-workspace-swap-monitors (hl--str mon1) (hl--str mon2))))

;; ---- actions: cursor and misc -----------------------------------------------

(define (hl-cursor-move x y)
  (= 0 (c-hl-cursor-move (exact->inexact x) (exact->inexact y))))

(define (hl-cursor-corner w corner)
  (= 0 (c-hl-cursor-corner (hl--wid w) corner)))

;; DANGER: quits Hyprland
(define (hl-exit)
  (= 0 (c-hl-exit)))

(define (hl-reload-config)
  (= 0 (c-hl-reload-config)))

(define (hl-force-renderer-reload)
  (= 0 (c-hl-force-renderer-reload)))

;; act: 'toggle/'on/'off; mon: #f (all) or a monitor name
(define (hl-dpms act . mon)
  (= 0 (c-hl-dpms (hl--togact act) (if (null? mon) "" (hl--str (car mon))))))

(define (hl-force-idle seconds)
  (= 0 (c-hl-force-idle (exact->inexact seconds))))

(define (hl-global action)
  (= 0 (c-hl-global (hl--str action))))

(define (hl-event data)
  (= 0 (c-hl-event (hl--str data))))

(define (hl-pass w)
  (= 0 (c-hl-pass (hl--wid w))))

;; mods: mask int (SHIFT 1 CAPS 2 CTRL 4 ALT 8 MOD2 16 MOD3 32 META 64 MOD5 128)
;; key: xkb keycode
(define (hl-send-shortcut mods key . w)
  (= 0 (c-hl-send-shortcut mods key (if (null? w) -1 (hl--wid (car w))))))

(define (hl-send-key-state mods key state . w)
  (= 0 (c-hl-send-key-state mods key state (if (null? w) -1 (hl--wid (car w))))))

;; interactive drag/resize for mouse binds: (hl-mouse "drag") / (hl-mouse "resize")
(define (hl-mouse action)
  (= 0 (c-hl-mouse (hl--str action))))

(define (hl-release-input-capture)
  (= 0 (c-hl-release-input-capture)))

;; send a message to the active workspace's layout (see custom-layouts)
(define (hl-layout-msg msg)
  (= 0 (c-hl-layout-message (hl--str msg))))

;; toggles; modes mirror Fullscreen::eFullscreenMode (1 maximized, 2 fullscreen)
;; optional second arg = mode (default 2); (hl-window-fullscreen w 1) maximizes
(define (hl-window-fullscreen w . opt)
  (= 0 (c-hl-window-fullscreen-toggle (hl--wid w) (if (null? opt) 2 (car opt)))))

(define (hl-window-maximize w)
  (= 0 (c-hl-window-fullscreen-toggle (hl--wid w) 1)))

;; explicit state: (internal-mode client-mode layout-aware?) — modes 0/1/2
(define (hl-window-fullscreen-state w internal client . layout-aware)
  (= 0 (c-hl-window-fullscreen-state (hl--wid w) internal client
         (if (null? layout-aware) 0 (if (car layout-aware) 1 0)))))

;; 0 = none, 1 = maximized, 2 = fullscreen, -1 = stale
(define (hl-window-fullscreen-mode w)
  (c-hl-window-fullscreen-mode (hl-window-id w)))

(define (hl-window-hidden? w)
  (= 1 (c-hl-window-hidden (hl-window-id w))))

(define (hl-window-pinned? w)
  (= 1 (c-hl-window-pinned (hl-window-id w))))

(define (hl-window-initial-class w)
  (c-hl-window-initial-class (hl-window-id w)))

(define (hl-window-initial-title w)
  (c-hl-window-initial-title (hl-window-id w)))

(define (hl-window-x11? w)
  (= 1 (c-hl-window-x11 (hl-window-id w))))

(define (hl-monitors)
  (let ((joined (c-hl-monitor-names)))
    (if (eq? joined #f)
        '()
        (hl--split-lines joined))))

(define (hl--window-listen which handler)
  (let ((id (c-hl-window-event-listen which)))
    (if (< id 0)
        (errorf 'hl-on-window "listener rejected, see compositor log")
        (hl--register id handler))))

(define (hl-on-window-open handler)
  (hl--window-listen 0 handler))

(define (hl-on-window-close handler)
  (hl--window-listen 1 handler))

;; handlers receive the window handle
(define (hl-on-window-title handler)
  (hl--window-listen 2 handler))

(define (hl-on-window-class handler)
  (hl--window-listen 3 handler))

(define (hl-on-window-urgent handler)
  (hl--window-listen 4 handler))

(define (hl-on-window-pin handler)
  (hl--window-listen 5 handler))

(define (hl-on-window-fullscreen handler)
  (hl--window-listen 6 handler))

;; fires when a window moves to a different workspace
(define (hl-on-window-move-to-workspace handler)
  (hl--window-listen 7 handler))

;; fires when the focused window changes
(define (hl-on-window-active handler)
  (hl--window-listen 8 handler))

;; handler signature: (lambda (w state) ...) — state is #t when minimized
(define (hl-on-window-minimize handler)
  (let ((id (c-hl-window-minimize-listen)))
    (if (< id 0)
        (errorf 'hl-on-window-minimize "listener rejected, see compositor log")
        (hl--register id handler))))

;; lifecycle: fires once when the session starts (its first render frame) and
;; once before exit. handlers registered after startup fire immediately.
(define (hl-on-start handler)
  (let ((id (c-hl-lifecycle-listen 0)))
    (if (< id 0)
        (errorf 'hl-on-start "listener rejected, see compositor log")
        (hl--register id handler))))

(define (hl-on-shutdown handler)
  (let ((id (c-hl-lifecycle-listen 1)))
    (if (< id 0)
        (errorf 'hl-on-shutdown "listener rejected, see compositor log")
        (hl--register id handler))))

(define (hl-on-config-reloaded handler)
  (let ((id (c-hl-config-reloaded-listen)))
    (if (< id 0)
        (errorf 'hl-on-config-reloaded "listener rejected, see compositor log")
        (hl--register id handler))))

;; identity, as in Lua's windowEq: true iff both handles refer to the same
;; live window (two handles for one window each get their own id)
(define (hl-window=? a b)
  (= 1 (c-hl-window-same (hl-window-id a) (hl-window-id b))))

(define (hl-unbind id)
  (= 0 (c-hl-unbind id)))

(define (hl-current-submap)
  (or (c-hl-current-submap) ""))

;; => (x . y), or #f
(define (hl-cursor-pos)
  (let ((s (c-hl-cursor-pos)))
    (if (eq? s #f)
        #f
        (let ((xy (map string->number (hl--split-lines s))))
          (cons (car xy) (cadr xy))))))

(define (hl-on-workspace-active handler)
  (let ((id (c-hl-workspace-active-listen)))
    (if (< id 0)
        (errorf 'hl-on-workspace-active "listener rejected, see compositor log")
        (hl--register id handler))))

(define (hl--reset)
  (set! hl--binds '()))

;; cross-reload state: lives in the bootstrap (evaluated ONCE), deliberately
;; OUTSIDE hl--reset's reach. Reloads wipe binds/timers/listeners/handles;
;; this assoc survives for as long as the interpreter does. Note that
;; generation-bound values stored here (e.g. window handles) still die with
;; their generation — the state survives, the resources do not.
(define hl--state '())

(define (hl-state-set! k v)
  (let ((entry (assq k hl--state)))
    (if entry
        (set-cdr! entry v)
        (set! hl--state (cons (cons k v) hl--state))))
  v)

(define (hl-state-ref k . default)
  (let ((entry (assq k hl--state)))
    (if entry
        (cdr entry)
        (if (null? default) #f (car default)))))

(define (hl-state-keys)
  (map car hl--state))

(set! hl--ready #t)
)scm";

namespace Config::Scheme::Internals {
    bool g_up          = false; // interpreter + bootstrap ready
    int  g_nextBindId  = 0;
    int  g_nextWindowId = 0;

    // window handles: id -> WEAK reference. The compositor destroys windows
    // whenever it wants; a stale handle is detected via lock() == nullptr.
    // Deliberately non-owning: a strong ref would keep a zombie window alive.
    std::unordered_map<int, PHLWINDOWREF> g_windows;
}

namespace Config::Scheme {

    using namespace Internals;

    static std::string                g_configPath;
    // submap registration context: binds created while set are scoped to it
    static std::string                g_regSubmap;
    static std::string                g_regSubmapReset;
    static int                        g_watchFd       = -1;    // inotify fd; dup'd into event loop waiters
    // binds keyed by their scheme id, so hl-unbind can target one
    static std::vector<std::pair<int, Keybinds::PBind>> g_binds;
    // scheme timers: C++ owns the CEventLoopTimer, Scheme owns the closure
    // (kept alive via the hl--binds assoc list, keyed by the same ids)
    static std::vector<std::pair<int, SP<CEventLoopTimer>>> g_timers;
    // event subscriptions: C++ owns the listener handles (dropping one
    // unsubscribes); Scheme owns the handler closures by id
    static std::vector<Hyprutils::Signal::CHyprSignalListener> g_submapListeners;
    static std::vector<Hyprutils::Signal::CHyprSignalListener> g_windowEventListeners;

    // lifecycle: the start event is dispatched by an init-time listener
    // (covers the plugin-auto-loaded-at-startup path, where the config load
    // precedes the first render frame).
    static bool                                        g_startSeen      = false;
    static std::vector<int>                            g_pendingStart;
    static std::vector<Hyprutils::Signal::CHyprSignalListener> g_lifecycleListeners;


    static Keybinds::SBindResult fireSchemeBind(int id) {
        if (!g_up)
            return {.success = false, .error = "scheme interpreter not initialized"};

        const ptr r = Scall1(Stop_level_value(Sstring_to_symbol("hl--fire")), Sinteger(id));
        if (r == Sfalse)
            return {.success = false, .error = "scheme keybind callback failed"};
        return {};
    }

    // fires a handler registered for id with no payload; errors contained
    static void fireScheme(int id) {
        if (!g_up)
            return;
        Scall1(Stop_level_value(Sstring_to_symbol("hl--fire")), Sinteger(id));
    }

    // called from Scheme via foreign-procedure; flags are raw eBindFlags bits,
    // assembled Scheme-side from the options alist
    static int hlSchemeBind(const char* mods, const char* key, int flags, const char* desc, const char* devices) {
        if (!g_up)
            return -1;

        std::vector<std::string> keys;
        std::istringstream       ss(mods ? mods : "");
        for (std::string tok; ss >> tok;)
            keys.emplace_back(std::move(tok));

        if (key && *key)
            keys.emplace_back(key);

        const int id = g_nextBindId++;

        Keybinds::SExtraBindArgs args;
        if (desc && *desc)
            args.metadata.description = desc;
        args.metadata.submap      = g_regSubmap;
        args.metadata.submapReset = g_regSubmapReset;
        if (devices && *devices) {
            std::istringstream ds(devices);
            for (std::string dev; std::getline(ds, dev, ',');)
                if (!dev.empty())
                    args.devices.emplace(dev);
        }

        auto bind = Keybinds::CBind::make(std::move(keys), sc<Keybinds::BindFlags>(flags), [id] { return fireSchemeBind(id); }, std::move(args));
        if (!bind) {
            LOG(Log::ERR, "[scheme] bind failed: {}", bind.error());
            return -1;
        }

        g_binds.emplace_back(id, Keybinds::mgr()->addBind(std::move(*bind)));
        return id;
    }

    // called from Scheme via foreign-procedure: remove one scheme bind
    static int hlSchemeUnbind(int id) {
        if (!g_up)
            return -1;

        const auto it = std::ranges::find_if(g_binds, [id](const auto& b) { return b.first == id; });
        if (it == g_binds.end())
            return -1;

        if (Keybinds::mgr())
            Keybinds::mgr()->removeBind(it->second);
        g_binds.erase(it);
        return 0;
    }

    // called from Scheme via foreign-procedure
    static int hlSchemeExec(const char* cmd) {
        if (!g_up || !cmd)
            return -1;

        return (int)Config::Supplementary::executor()->spawn(cmd).value_or(-1);
    }

    // fires a handler registered for id with a string payload; all errors are
    // contained inside hl--fire-str's guard
    static void fireSchemeStr(int id, const std::string& arg) {
        if (!g_up)
            return;

        Scall2(Stop_level_value(Sstring_to_symbol("hl--fire-str")), Sinteger(id), Sstring_utf8(arg.c_str(), arg.size()));
    }

    // events carrying window payloads: the window crosses as a fresh handle id
    static void fireSchemeWin(int id, PHLWINDOW window) {
        if (!g_up || !window)
            return;

        const int winId = g_nextWindowId++;
        g_windows.emplace(winId, PHLWINDOWREF(window));

        Scall2(Stop_level_value(Sstring_to_symbol("hl--fire-win")), Sinteger(id), Sinteger(winId));
    }

    // called from Scheme via foreign-procedure; repeat != 0 re-arms forever
    // (or until the callback errors, which stops zombie loops)
    static int hlSchemeTimer(int ms, int repeat) {
        if (!g_up || !g_pEventLoopManager || ms < 0)
            return -1;

        const int id = g_nextBindId++;

        auto shared = makeShared<CEventLoopTimer>(std::chrono::milliseconds(ms),
            [id, ms, repeat](SP<CEventLoopTimer> self, void*) {
                const auto result = fireSchemeBind(id);

                if (repeat && result.success) {
                    self->updateTimeout(std::chrono::milliseconds(ms));
                    return;
                }

                // one-shot done, or the callback failed: tear down.
                // onTimerFire dispatches over a copy of the timer list, so
                // removing ourselves here is safe.
                self->cancel();
                if (g_pEventLoopManager)
                    g_pEventLoopManager->removeTimer(self);
                std::erase_if(g_timers, [self](const auto& t) { return t.second == self; });
            },
            nullptr);

        g_pEventLoopManager->addTimer(shared);
        g_timers.emplace_back(id, shared);
        return id;
    }

    // called from Scheme via foreign-procedure: focused window title, or #f
    static ptr hlSchemeActiveTitle() {
        if (!g_up)
            return Sfalse;

        const auto window = Desktop::focusState()->window();
        if (!window)
            return Sfalse;

        const auto title = window->metadata().title();
        return Sstring_utf8(title.c_str(), title.size());
    }

    // called from Scheme via foreign-procedure: newline-joined workspace
    // display names, or #f. split on the Scheme side.
    static ptr hlSchemeWorkspaceNames() {
        if (!g_up)
            return Sfalse;

        std::string joined;
        for (const auto& wsRef : State::Workspace::state()->workspaces()) {
            const auto ws = wsRef.lock();
            if (!ws)
                continue;
            if (!joined.empty())
                joined += '\n';
            joined += ws->displayName();
        }

        if (joined.empty())
            return Sfalse;

        return Sstring_utf8(joined.c_str(), joined.size());
    }

    // called from Scheme via foreign-procedure: subscribe to submap changes
    static int hlSchemeSubmapListen() {
        if (!g_up)
            return -1;

        const int id = g_nextBindId++;
        g_submapListeners.emplace_back(Event::bus()->m_events.keybinds.submap.listen([id](const std::string& name) {
            fireSchemeStr(id, name);
        }));
        return id;
    }

    // resolves a handle id to the live window, or null if stale (dead or
    // unknown). lazily drops dead entries while we're here.
    static std::optional<PHLWINDOW> actionWindow(int id);

    static PHLWINDOW windowFromId(int id) {
        const auto it = g_windows.find(id);
        if (it == g_windows.end())
            return nullptr;

        auto window = it->second.lock();
        if (!window)
            g_windows.erase(it);

        return window;
    }

    static int hlSchemeActiveWindowId() {
        if (!g_up)
            return -1;

        const auto window = Desktop::focusState()->window();
        if (!window)
            return -1;

        const int id = g_nextWindowId++;
        g_windows.emplace(id, PHLWINDOWREF(window));
        return id;
    }

    static ptr hlSchemeWindowIds() {
        if (!g_up)
            return Sfalse;

        std::string joined;
        for (const auto& w : Desktop::windowState()->windows()) {
            if (!w->mapped())
                continue;

            if (!joined.empty())
                joined += '\n';

            const int id = g_nextWindowId++;
            g_windows.emplace(id, PHLWINDOWREF(w));
            joined += std::to_string(id);
        }

        if (joined.empty())
            return Sfalse;

        return Sstring_utf8(joined.c_str(), joined.size());
    }

    static ptr hlSchemeWindowTitle(int id) {
        if (!g_up)
            return Sfalse;

        const auto window = windowFromId(id);
        if (!window)
            return Sfalse;

        const auto title = window->metadata().title();
        return Sstring_utf8(title.c_str(), title.size());
    }

    static int hlSchemeWindowAlive(int id) {
        if (!g_up)
            return 0;

        return windowFromId(id) ? 1 : 0;
    }

    static int hlSchemeWindowClose(int id) {
        if (!g_up)
            return -1;

        const auto window = actionWindow(id);
        if (id >= 0 && !window)
            return -1;
        if (!window)
            return -1;

        return Config::Actions::closeWindow(window) ? 0 : -2;
    }

    static ptr hlSchemeWindowClass(int id) {
        if (!g_up)
            return Sfalse;

        const auto window = windowFromId(id);
        if (!window)
            return Sfalse;

        const auto s = window->metadata().appID();
        return Sstring_utf8(s.c_str(), s.size());
    }

    static ptr hlSchemeWindowWorkspaceName(int id) {
        if (!g_up)
            return Sfalse;

        const auto window = windowFromId(id);
        if (!window || !window->m_workspace)
            return Sfalse;

        const auto s = window->m_workspace->displayName();
        return Sstring_utf8(s.c_str(), s.size());
    }

    static ptr hlSchemeWindowMonitorName(int id) {
        if (!g_up)
            return Sfalse;

        const auto window = windowFromId(id);
        if (!window)
            return Sfalse;

        const auto monitor = State::monitorState()->query().id(window->monitorID()).run();
        if (!monitor)
            return Sfalse;

        return Sstring_utf8(monitor->m_name.c_str(), monitor->m_name.size());
    }

    static int hlSchemeWindowFloating(int id) {
        if (!g_up)
            return 0;

        const auto window = windowFromId(id);
        return (window && window->isFloating()) ? 1 : 0;
    }

    static ptr hlSchemeWindowSize(int id) {
        if (!g_up)
            return Sfalse;

        const auto window = windowFromId(id);
        if (!window)
            return Sfalse;

        const auto sz = window->size(Desktop::View::IGeometric::GEOMETRIC_GOAL);
        const auto s  = std::to_string((int)sz.x) + "\n" + std::to_string((int)sz.y);
        return Sstring_utf8(s.c_str(), s.size());
    }

    static int hlSchemeWindowPid(int id) {
        if (!g_up)
            return -1;

        const auto window = windowFromId(id);
        if (!window)
            return -1;

        return (int)window->backend().pid();
    }

    static int hlSchemeWindowFocus(int id) {
        if (!g_up)
            return -1;

        const auto window = actionWindow(id);
        if (!window)
            return -1;

        return Config::Actions::focus(*window) ? 0 : -2;
    }

    static int hlSchemeWindowFloat(int id) {
        if (!g_up)
            return -1;

        const auto window = actionWindow(id);
        if (!window)
            return -1;

        return Config::Actions::floatWindow(Config::Actions::TOGGLE_ACTION_TOGGLE, *window) ? 0 : -2;
    }

    static int hlSchemeWindowMoveToWorkspace(int id, const char* name) {
        if (!g_up || !name)
            return -1;

        const auto window = actionWindow(id);
        if (!window)
            return -1;
        const PHLWINDOW w = *window;

        const auto target = State::Workspace::resolver()->getWorkspaceTargetFromString(name);
        if (!target.valid())
            return -2;

        auto ws = State::Workspace::state()->find(target);
        if (!ws) {
            // create missing workspaces on the window's own monitor
            // (isEmpty=false, like Lua's resolveWorkspaceStr — an empty-flagged
            // workspace gets swept before the window lands in it)
            const auto mon = w->m_workspace ? w->m_workspace->m_monitor.lock() : Desktop::focusState()->monitor();
            ws             = State::Workspace::state()->create(target, mon, false);
        }
        if (!ws)
            return -2;

        return Config::Actions::moveToWorkspace(ws, false, w) ? 0 : -2;
    }

    // toggles the given fullscreen mode (FSMODE_FULLSCREEN=2, FSMODE_MAXIMIZED=1),
    // mirroring the toggle logic in Lua's dsp_fullscreenWindowWithAction
    static int hlSchemeWindowFullscreenToggle(int id, int modeRaw) {
        if (!g_up)
            return -1;

        const auto window = actionWindow(id);
        if (!window)
            return -1;
        const PHLWINDOW w = *window;

        const auto mode = sc<Fullscreen::eFullscreenMode>(modeRaw);
        if (Fullscreen::controller()->isFullscreen(w, mode))
            return Config::Actions::fullscreenWindow(Fullscreen::FSMODE_NONE, false, w) ? 0 : -2;

        return Config::Actions::fullscreenWindow(mode, false, w) ? 0 : -2;
    }

    static int hlSchemeWindowFullscreenMode(int id) {
        if (!g_up)
            return -1;

        const auto window = windowFromId(id);
        if (!window)
            return -1;

        return sc<int>(Fullscreen::controller()->getFullscreenModes(window).internal);
    }

    static int hlSchemeWindowHidden(int id) {
        if (!g_up)
            return 0;

        const auto window = windowFromId(id);
        return (window && window->isHidden()) ? 1 : 0;
    }

    static int hlSchemeWindowPinned(int id) {
        if (!g_up)
            return 0;

        const auto window = windowFromId(id);
        return (window && (window->m_state & Desktop::View::WINDOW_STATE_PINNED)) ? 1 : 0;
    }

    // ---- actions: the dispatcher surface --------------------------------------
    // Each handler wraps one Config::Actions call — the same layer the Lua
    // hl.dsp.* dispatchers use. Window args take a scheme handle id, or -1
    // for the active window.

    static std::optional<PHLWINDOW> actionWindow(int id) {
        if (id >= 0)
            return windowFromId(id);
        return Desktop::focusState()->window();
    }

    static int actionResult(const char* name, Config::Actions::ActionResult result) {
        if (result)
            return 0;
        LOG(Log::ERR, "[scheme] {} failed: {}", name, result.error().message);
        return -2;
    }

    static Math::eDirection actionDir(const char* s) {
        return Math::fromChar(s && *s ? s[0] : 'x');
    }

    static PHLMONITOR monitorFromName(const char* name) {
        if (!name || !*name)
            return nullptr;
        for (const auto& m : State::monitorState()->monitors())
            if (m->m_name == name)
                return m;
        return nullptr;
    }

    static PHLWORKSPACE workspaceFromName(const char* name) {
        if (!name || !*name)
            return nullptr;
        return State::Workspace::state()->query().input(std::string(name)).run();
    }

    static int hlSchemeFocusWorkspace(const char* ws) {
        if (!g_up)
            return -1;
        return actionResult("focus-workspace", Config::Actions::changeWorkspace(std::string(ws ? ws : "")));
    }

    static int hlSchemeFocusDirection(const char* dir) {
        if (!g_up)
            return -1;
        return actionResult("focus-direction", Config::Actions::moveFocus(actionDir(dir)));
    }

    static int hlSchemeFocusMonitor(const char* name) {
        if (!g_up)
            return -1;
        const auto mon = monitorFromName(name);
        if (!mon) {
            LOG(Log::ERR, "[scheme] focus-monitor: no monitor named {}", name ? name : "");
            return -1;
        }
        return actionResult("focus-monitor", Config::Actions::focusMonitor(mon));
    }

    static int hlSchemeFocusLast() {
        if (!g_up)
            return -1;
        return actionResult("focus-last", Config::Actions::focusCurrentOrLast());
    }

    static int hlSchemeFocusUrgent() {
        if (!g_up)
            return -1;
        return actionResult("focus-urgent", Config::Actions::focusUrgentOrLast());
    }

    static int hlSchemeWindowMoveDirection(int id, const char* dir) {
        if (!g_up)
            return -1;
        return actionResult("window-move-direction", Config::Actions::moveInDirection(actionDir(dir), actionWindow(id)));
    }

    static int hlSchemeWindowSwapDirection(int id, const char* dir) {
        if (!g_up)
            return -1;
        return actionResult("window-swap-direction", Config::Actions::swapInDirection(actionDir(dir), actionWindow(id)));
    }

    static int hlSchemeWindowSwapNext(int id, int prev) {
        if (!g_up)
            return -1;
        return actionResult("window-swap-next", Config::Actions::swapNext(prev == 0, actionWindow(id)));
    }

    static int hlSchemeWindowSwapWith(int id, int otherId) {
        if (!g_up)
            return -1;
        const auto other = windowFromId(otherId);
        if (!other)
            return -1;
        return actionResult("window-swap-with", Config::Actions::swapWith(other, actionWindow(id)));
    }

    // filter: 0 = all, 1 = tiled only, 2 = floating only
    static int hlSchemeWindowCycle(int id, int next, int filter) {
        if (!g_up)
            return -1;
        std::optional<bool> tiled, floating;
        if (filter == 1)
            tiled = true;
        else if (filter == 2)
            floating = true;
        return actionResult("window-cycle", Config::Actions::cycleNext(next != 0, tiled, floating, actionWindow(id)));
    }

    static int hlSchemeWindowCenter(int id) {
        if (!g_up)
            return -1;
        return actionResult("window-center", Config::Actions::center(actionWindow(id)));
    }

    static int hlSchemeWindowResizePx(int id, double w, double h, int relative) {
        if (!g_up)
            return -1;
        return actionResult("window-resize", Config::Actions::resize(Vector2D{w, h}, relative != 0, actionWindow(id)));
    }

    static int hlSchemeWindowMovePx(int id, double x, double y, int relative) {
        if (!g_up)
            return -1;
        return actionResult("window-move", Config::Actions::move(Vector2D{x, y}, relative != 0, actionWindow(id)));
    }

    // act: 0 = toggle, 1 = on, 2 = off
    static int hlSchemeWindowFloatAct(int id, int act) {
        if (!g_up)
            return -1;
        return actionResult("window-float", Config::Actions::floatWindow(sc<Config::Actions::eTogglableAction>(act), actionWindow(id)));
    }

    // act: 0 = toggle, 1 = on, 2 = off
    static int hlSchemeWindowPinAct(int id, int act) {        if (!g_up)
            return -1;
        return actionResult("window-pin", Config::Actions::pinWindow(sc<Config::Actions::eTogglableAction>(act), actionWindow(id)));
    }

    static int hlSchemeWindowPseudo(int id, int act) {
        if (!g_up)
            return -1;
        return actionResult("window-pseudo", Config::Actions::pseudoWindow(sc<Config::Actions::eTogglableAction>(act), actionWindow(id)));
    }

    static int hlSchemeWindowKill(int id) {
        if (!g_up)
            return -1;
        return actionResult("window-kill", Config::Actions::killWindow(actionWindow(id)));
    }

    static int hlSchemeWindowSignal(int id, int sig) {
        if (!g_up)
            return -1;
        return actionResult("window-signal", Config::Actions::signalWindow(sig, actionWindow(id)));
    }

    static int hlSchemeWindowZOrder(int id, const char* mode) {
        if (!g_up)
            return -1;
        return actionResult("window-zorder", Config::Actions::alterZOrder(std::string(mode ? mode : ""), actionWindow(id)));
    }

    static int hlSchemeWindowSetProp(int id, const char* prop, const char* val) {
        if (!g_up)
            return -1;
        return actionResult("window-set-prop", Config::Actions::setProp(std::string(prop ? prop : ""), std::string(val ? val : ""), actionWindow(id)));
    }

    static int hlSchemeWindowTag(int id, const char* tag) {
        if (!g_up)
            return -1;
        return actionResult("window-tag", Config::Actions::tag(std::string(tag ? tag : ""), actionWindow(id)));
    }

    static int hlSchemeWindowClearTags(int id) {
        if (!g_up)
            return -1;
        return actionResult("window-clear-tags", Config::Actions::clearTags(actionWindow(id)));
    }

    static int hlSchemeToggleSwallow() {
        if (!g_up)
            return -1;
        return actionResult("toggle-swallow", Config::Actions::toggleSwallow());
    }

    static int hlSchemeGroupToggle(int id) {
        if (!g_up)
            return -1;
        return actionResult("group-toggle", Config::Actions::toggleGroup(actionWindow(id)));
    }

    static int hlSchemeGroupCycle(int id, int prev) {
        if (!g_up)
            return -1;
        return actionResult("group-cycle", Config::Actions::changeGroupActive(prev == 0, actionWindow(id)));
    }

    static int hlSchemeGroupIndex(int id, int index) {
        if (!g_up)
            return -1;
        return actionResult("group-index", Config::Actions::setGroupActive(index, actionWindow(id)));
    }

    static int hlSchemeGroupMoveWindow(int id, int prev) {
        if (!g_up)
            return -1;
        return actionResult("group-move-window", Config::Actions::moveGroupWindow(prev == 0));
    }

    static int hlSchemeGroupLock(int act) {
        if (!g_up)
            return -1;
        return actionResult("group-lock", Config::Actions::lockGroups(sc<Config::Actions::eTogglableAction>(act)));
    }

    static int hlSchemeGroupLockActive(int act) {
        if (!g_up)
            return -1;
        return actionResult("group-lock-active", Config::Actions::lockActiveGroup(sc<Config::Actions::eTogglableAction>(act)));
    }

    static int hlSchemeWindowIntoGroup(int id, const char* dir) {
        if (!g_up)
            return -1;
        return actionResult("window-into-group", Config::Actions::moveIntoGroup(actionDir(dir), actionWindow(id)));
    }

    static int hlSchemeWindowOutOfGroup(int id, const char* dir) {
        if (!g_up)
            return -1;
        return actionResult("window-out-of-group", Config::Actions::moveOutOfGroup(actionDir(dir), actionWindow(id)));
    }

    static int hlSchemeWindowIntoOrCreateGroup(int id, const char* dir) {
        if (!g_up)
            return -1;
        return actionResult("window-into-or-create-group", Config::Actions::moveIntoOrCreateGroup(actionDir(dir), actionWindow(id)));
    }

    static int hlSchemeWindowDenyFromGroup(int id, int act) {
        if (!g_up)
            return -1;
        return actionResult("window-deny-from-group", Config::Actions::denyWindowFromGroup(sc<Config::Actions::eTogglableAction>(act)));
    }

    static int hlSchemeWorkspaceRename(const char* oldName, const char* newName) {
        if (!g_up)
            return -1;
        const auto ws = workspaceFromName(oldName);
        if (!ws) {
            LOG(Log::ERR, "[scheme] workspace-rename: no workspace named {}", oldName ? oldName : "");
            return -1;
        }
        return actionResult("workspace-rename", Config::Actions::renameWorkspace(ws, std::string(newName ? newName : "")));
    }

    static int hlSchemeWorkspaceMoveMonitor(const char* wsName, const char* monName) {
        if (!g_up)
            return -1;
        const auto ws  = workspaceFromName(wsName);
        const auto mon = monitorFromName(monName);
        if (!ws || !mon) {
            LOG(Log::ERR, "[scheme] workspace-move-to-monitor: no workspace named {} or monitor named {}", wsName ? wsName : "", monName ? monName : "");
            return -1;
        }
        return actionResult("workspace-move-to-monitor", Config::Actions::moveToMonitor(ws, mon));
    }

    static int hlSchemeWorkspaceToggleSpecial(const char* wsName) {
        if (!g_up)
            return -1;
        const auto ws = workspaceFromName(wsName);
        if (!ws) {
            LOG(Log::ERR, "[scheme] workspace-toggle-special: no workspace named {}", wsName ? wsName : "");
            return -1;
        }
        return actionResult("workspace-toggle-special", Config::Actions::toggleSpecial(ws));
    }

    static int hlSchemeWorkspaceSwapMonitors(const char* mon1, const char* mon2) {
        if (!g_up)
            return -1;
        const auto a = monitorFromName(mon1);
        const auto b = monitorFromName(mon2);
        if (!a || !b) {
            LOG(Log::ERR, "[scheme] workspace-swap-monitors: no monitor named {} or {}", mon1 ? mon1 : "", mon2 ? mon2 : "");
            return -1;
        }
        return actionResult("workspace-swap-monitors", Config::Actions::swapActiveWorkspaces(a, b));
    }

    static int hlSchemeCursorMove(double x, double y) {
        if (!g_up)
            return -1;
        return actionResult("cursor-move", Config::Actions::moveCursor(Vector2D{x, y}));
    }

    static int hlSchemeCursorCorner(int id, int corner) {
        if (!g_up)
            return -1;
        return actionResult("cursor-move-to-corner", Config::Actions::moveCursorToCorner(corner, actionWindow(id)));
    }

    static int hlSchemeExit() {
        if (!g_up)
            return -1;
        return actionResult("exit", Config::Actions::exit());
    }

    static int hlSchemeReloadConfig() {
        if (!g_up)
            return -1;
        return actionResult("reload-config", Config::Actions::reloadConfig());
    }

    static int hlSchemeForceRendererReload() {
        if (!g_up)
            return -1;
        return actionResult("force-renderer-reload", Config::Actions::forceRendererReload());
    }

    static int hlSchemeDpms(int act, const char* monName) {
        if (!g_up)
            return -1;
        std::optional<PHLMONITOR> mon;
        if (monName && *monName) {
            mon = monitorFromName(monName);
            if (!mon) {
                LOG(Log::ERR, "[scheme] dpms: no monitor named {}", monName);
                return -1;
            }
        }
        return actionResult("dpms", Config::Actions::dpms(sc<Config::Actions::eTogglableAction>(act), mon));
    }

    static int hlSchemeForceIdle(double seconds) {
        if (!g_up)
            return -1;
        return actionResult("force-idle", Config::Actions::forceIdle(sc<float>(seconds)));
    }

    static int hlSchemeGlobal(const char* action) {
        if (!g_up)
            return -1;
        return actionResult("global", Config::Actions::global(std::string(action ? action : "")));
    }

    static int hlSchemeEvent(const char* data) {
        if (!g_up)
            return -1;
        return actionResult("event", Config::Actions::event(std::string(data ? data : "")));
    }

    static int hlSchemePass(int id) {
        if (!g_up)
            return -1;
        return actionResult("pass", Config::Actions::pass(actionWindow(id)));
    }

    static int hlSchemeSendShortcut(int mask, int key, int id) {
        if (!g_up)
            return -1;
        return actionResult("send-shortcut", Config::Actions::pass(Input::ModifierMask(sc<Input::eKeyboardModifiers>(mask)), sc<uint32_t>(key), actionWindow(id)));
    }

    static int hlSchemeSendKeyState(int mask, int key, int state, int id) {
        if (!g_up)
            return -1;
        return actionResult("send-key-state", Config::Actions::sendKeyState(Input::ModifierMask(sc<Input::eKeyboardModifiers>(mask)), sc<uint32_t>(key), sc<uint32_t>(state), actionWindow(id)));
    }

    static int hlSchemeMouse(const char* action) {
        if (!g_up)
            return -1;
        return actionResult("mouse", Config::Actions::mouse(std::string(action ? action : "")));
    }

    static int hlSchemeReleaseInputCapture() {
        if (!g_up)
            return -1;
        return actionResult("release-input-capture", Config::Actions::releaseInputCapture());
    }

    // explicit fullscreen: internal and client modes, layout-aware flag
    static int hlSchemeWindowFullscreenState(int id, int internalMode, int clientMode, int layoutAware) {
        if (!g_up)
            return -1;
        const auto w = actionWindow(id);
        if (id >= 0 && !w)
            return -1;
        return actionResult("window-fullscreen-state",
            Config::Actions::fullscreenWindow(sc<Fullscreen::eFullscreenMode>(internalMode), sc<Fullscreen::eFullscreenMode>(clientMode), layoutAware != 0, w));
    }

    static int hlSchemeLayoutMessage(const char* msg) {
        if (!g_up)
            return -1;
        return actionResult("layout-msg", Config::Actions::layoutMessage(std::string(msg ? msg : "")));
    }

    static ptr hlSchemeWindowInitialClass(int id) {
        if (!g_up)
            return Sfalse;

        const auto window = windowFromId(id);
        if (!window)
            return Sfalse;

        const auto s = window->metadata().initialAppID();
        return Sstring_utf8(s.c_str(), s.size());
    }

    static ptr hlSchemeWindowInitialTitle(int id) {
        if (!g_up)
            return Sfalse;

        const auto window = windowFromId(id);
        if (!window)
            return Sfalse;

        const auto s = window->metadata().initialTitle();
        return Sstring_utf8(s.c_str(), s.size());
    }

    static int hlSchemeWindowX11(int id) {
        if (!g_up)
            return 0;

        const auto window = windowFromId(id);
        return (window && window->backend().isX11()) ? 1 : 0;
    }

    static ptr hlSchemeMonitorNames() {
        if (!g_up)
            return Sfalse;

        std::string joined;
        for (const auto& m : State::monitorState()->monitors()) {
            if (!joined.empty())
                joined += '\n';
            joined += m->m_name;
        }

        if (joined.empty())
            return Sfalse;

        return Sstring_utf8(joined.c_str(), joined.size());
    }

    static int hlSchemeWindowEventListen(int which) {
        if (!g_up)
            return -1;

        const int id = g_nextBindId++;

        switch (which) {
            case 0: g_windowEventListeners.emplace_back(Event::bus()->m_events.window.openLate.listen([id](PHLWINDOW w) { fireSchemeWin(id, w); })); break;
            case 1: g_windowEventListeners.emplace_back(Event::bus()->m_events.window.close.listen([id](PHLWINDOW w) { fireSchemeWin(id, w); })); break;
            case 2: g_windowEventListeners.emplace_back(Event::bus()->m_events.window.title.listen([id](PHLWINDOW w) { fireSchemeWin(id, w); })); break;
            case 3: g_windowEventListeners.emplace_back(Event::bus()->m_events.window.class_.listen([id](PHLWINDOW w) { fireSchemeWin(id, w); })); break;
            case 4: g_windowEventListeners.emplace_back(Event::bus()->m_events.window.urgent.listen([id](PHLWINDOW w) { fireSchemeWin(id, w); })); break;
            case 5: g_windowEventListeners.emplace_back(Event::bus()->m_events.window.pin.listen([id](PHLWINDOW w) { fireSchemeWin(id, w); })); break;
            case 6: g_windowEventListeners.emplace_back(Event::bus()->m_events.window.fullscreen.listen([id](PHLWINDOW w) { fireSchemeWin(id, w); })); break;
            case 7: g_windowEventListeners.emplace_back(Event::bus()->m_events.window.moveToWorkspace.listen([id](PHLWINDOW w, PHLWORKSPACE ws) { fireSchemeWin(id, w); })); break;
            case 8: g_windowEventListeners.emplace_back(Event::bus()->m_events.window.active.listen([id](PHLWINDOW w, Desktop::eFocusReason) { fireSchemeWin(id, w); })); break;
            default: break;
        }

        return id;
    }

    // minimize fires with (window, state): pass the bool as a second arg
    static int hlSchemeWindowMinimizeListen() {
        if (!g_up)
            return -1;

        const int id = g_nextBindId++;
        g_windowEventListeners.emplace_back(Event::bus()->m_events.window.minimize.listen([id](PHLWINDOW w, bool state) {
            if (!g_up || !w)
                return;
            const int winId = g_nextWindowId++;
            g_windows.emplace(winId, PHLWINDOWREF(w));
            Scall3(Stop_level_value(Sstring_to_symbol("hl--fire-win-state")), Sinteger(id), Sinteger(winId), state ? Strue : Sfalse);
        }));
        return id;
    }

    // lifecycle: 0 = start (session's first render frame), 1 = shutdown
    // (the exit action). Matches upstream: a handler registered after start
    // already fired (only possible when the plugin itself loaded before the
    // first frame) runs on the next loop pass.
    static int hlSchemeLifecycleListen(int which) {
        if (!g_up)
            return -1;

        const int id = g_nextBindId++;

        if (which == 0) {
            if (g_startSeen) {
                // the handler is registered by Scheme only AFTER this call
                // returns, so the immediate fire must wait for the next pass
                if (g_pEventLoopManager)
                    g_pEventLoopManager->doLater([id] { fireScheme(id); });
            } else
                g_pendingStart.emplace_back(id);
        } else
            g_windowEventListeners.emplace_back(Event::bus()->m_events.exit.listen([id] { fireScheme(id); }));

        return id;
    }

    static int hlSchemeConfigReloadedListen() {
        if (!g_up)
            return -1;

        const int id = g_nextBindId++;
        g_windowEventListeners.emplace_back(Event::bus()->m_events.config.reloaded.listen([id] { fireScheme(id); }));
        return id;
    }

    // identity, mirroring Lua's windowEq: two handles are the same window
    // iff they lock to the same underlying object. Dead handles are never
    // "the same" as anything.
    static int hlSchemeWindowSame(int idA, int idB) {
        if (!g_up)
            return 0;

        const auto a = windowFromId(idA);
        const auto b = windowFromId(idB);
        return (a && b && a.get() == b.get()) ? 1 : 0;
    }

    static ptr hlSchemeCurrentSubmap() {
        if (!g_up || !Keybinds::mgr())
            return Sfalse;

        const auto submap = std::string(Keybinds::mgr()->currentSubmap());
        return Sstring_utf8(submap.c_str(), submap.size());
    }

    static ptr hlSchemeCursorPos() {
        if (!g_up || !Pointer::mgr())
            return Sfalse;

        const auto pos = Pointer::mgr()->untransformedPosition();
        const auto s   = std::to_string((int)pos.x) + "\n" + std::to_string((int)pos.y);
        return Sstring_utf8(s.c_str(), s.size());
    }

    static int hlSchemeWorkspaceActiveListen() {
        if (!g_up)
            return -1;

        const int id = g_nextBindId++;
        g_windowEventListeners.emplace_back(Event::bus()->m_events.workspace.active.listen([id](PHLWORKSPACE ws) {
            if (ws)
                fireSchemeStr(id, ws->displayName());
        }));
        return id;
    }

    static void reloadScheme() {
        if (!g_up || g_configPath.empty())
            return;

        // lua config reloads clear every bind in the registry; drop our stale handles.
        for (const auto& [id, b] : g_binds) {
            if (Keybinds::mgr())
                Keybinds::mgr()->removeBind(b);
        }
        g_binds.clear();

        // timers from the previous generation must not fire into the new one
        if (g_pEventLoopManager) {
            for (const auto& [id, t] : g_timers) {
                t->cancel();
                g_pEventLoopManager->removeTimer(t);
            }
        }
        g_timers.clear();

        // drop event subscriptions; the new generation re-registers
        g_submapListeners.clear();
        g_windowEventListeners.clear();

        // old-generation window handles die with their generation
        g_windows.clear();

        // layouts unregister + re-register with the new generation
        Layouts::clear();

        // re-binding the config value caches: CConfigValue readers (e.g. the
        // layout matcher's general:layout) hold copies made at first use, and
        // this reload may have changed what they should see.
        CConfigValueBase::flushCaches();

        Scall0(Stop_level_value(Sstring_to_symbol("hl--reset")));

        const ptr r = Scall1(Stop_level_value(Sstring_to_symbol("hl--load")), Sstring(g_configPath.c_str()));
        if (r == Sfalse)
            LOG(Log::ERR, "[scheme] failed to load {}", g_configPath);
        else
            LOG(Log::INFO, "[scheme] loaded {}", g_configPath);
    }

    static void drainWatch() {
        alignas(struct inotify_event) char buf[4096];
        bool                               dirty = false;

        while (true) {
            const ssize_t n = read(g_watchFd, buf, sizeof(buf));
            if (n <= (ssize_t)sizeof(struct inotify_event))
                break;

            for (ssize_t off = 0; off + (ssize_t)sizeof(struct inotify_event) <= n;) {
                const auto* ev = reinterpret_cast<const struct inotify_event*>(&buf[off]);
                if ((ev->mask & (IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE)) && ev->len > 0 &&
                    g_configPath.ends_with(ev->name))
                    dirty = true;
                off += (ssize_t)sizeof(struct inotify_event) + ev->len;
            }
        }

        if (dirty)
            reloadScheme();
    }

    // event loop waiters are one-shot: re-arm after every wakeup.
    static void armWatch() {
        if (g_watchFd < 0 || !g_pEventLoopManager)
            return;

        g_pEventLoopManager->doOnReadable(CFileDescriptor(dup(g_watchFd)), [] {
            drainWatch();
            armWatch();
        });
    }

    static void setupWatch() {
        const auto dir = std::filesystem::path(g_configPath).parent_path().string();

        g_watchFd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
        if (g_watchFd < 0)
            return;

        if (inotify_add_watch(g_watchFd, dir.c_str(), IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE) < 0) {
            close(g_watchFd);
            g_watchFd = -1;
            return;
        }

        armWatch();
    }

    static std::string writeSchemeFile(const char* name, const char* content) {
        const char* runtime = getenv("XDG_RUNTIME_DIR");
        const auto  path   = std::filesystem::path(runtime ? runtime : "/tmp") / name;

        std::ofstream out(path);
        out << content;

        return path.string();
    }

    static std::string userConfigPath() {
        const char* cfg = getenv("XDG_CONFIG_HOME");
        std::string base;
        if (cfg && *cfg)
            base = cfg;
        else if (const char* home = getenv("HOME"); home && *home)
            base = std::string(home) + "/.config";
        else
            return {};

        return base + "/hypr/hyprland.scm";
    }

    // reimplements Compositor.cpp's handleUnrecoverableSignal using the
    // exported CrashReporter::createAndSaveCrash
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

    // teardown for plugin unload: everything pointing into this .so must be
    // unregistered before hyprpm dlcloses it
    static SP<IPC::Socket1::SCommand> g_schemeIpcCommand;
    static Hyprutils::Signal::CHyprSignalListener g_reloadListener;

    void shutdown() {
        if (g_schemeIpcCommand) {
            IPC::Socket1::sock()->unregisterCommand(g_schemeIpcCommand);
            g_schemeIpcCommand.reset();
        }
        g_reloadListener.reset();
        g_lifecycleListeners.clear();
        Layouts::clear();
        // remove our binds: the compositor keeps running without us
        for (const auto& [id, b] : g_binds) {
            if (Keybinds::mgr())
                Keybinds::mgr()->removeBind(b);
        }
        g_binds.clear();
        // the interpreter stays alive: Chez does not survive a teardown +
        // re-init inside the compositor (the second Sbuild_heap hangs), so
        // a re-load of the plugin re-attaches to the live interpreter
        g_up = false;
    }

    void init() {
        static bool done = false;
        if (done) {
            // soft reload: the interpreter and bootstrap are still alive;
            // re-attach the plumbing that shutdown() removed
            g_up = true;
            if (g_pEventLoopManager && IPC::Socket1::sock()) {
                g_schemeIpcCommand = IPC::Socket1::sock()->registerCommand(IPC::Socket1::SCommand{
                    .name    = "scheme",
                    .match   = IPC::Socket1::COMMAND_MATCH_PREFIX,
                    .handler = [](const IPC::Socket1::SRequest& req) {
                        auto code = req.command.substr(req.command.find_first_of(' ') + 1);
                        const ptr r = Scall1(Stop_level_value(Sstring_to_symbol("hl--eval")), Sstring_utf8(code.c_str(), code.size()));
                        std::string out;
                        if (Sstringp(r)) {
                            for (iptr i = 0; i < Sstring_length(r); ++i)
                                out += (char)Sstring_ref(r, i);
                        }
                        return IPC::Socket1::SResponse(out);
                    }});
            }
            g_reloadListener = Event::bus()->m_events.config.reloaded.listen([] { reloadScheme(); });
            g_lifecycleListeners.emplace_back(Event::bus()->m_events.start.listen([] {
                g_startSeen = true;
                auto pending = std::move(g_pendingStart);
                g_pendingStart.clear();
                for (const auto id : pending)
                    fireScheme(id);
            }));
            reloadScheme();
            return;
        }
        done = true;

        g_configPath = userConfigPath();
        if (g_configPath.empty() || !std::filesystem::exists(g_configPath)) {
            LOG(Log::INFO, "[scheme] no hyprland.scm found, scheme scripting disabled");
            return;
        }

        // pin ourselves: bump the dlopen refcount so the compositor's
        // dlclose on unload never unmaps the live interpreter
        {
            Dl_info self{};
            if (dladdr((void*)&init, &self) && self.dli_fname)
                dlopen(self.dli_fname, RTLD_NOW | RTLD_NOLOAD);
        }
        Sscheme_init(nullptr);
        // boot files live next to the plugin (.so dir), with the install
        // prefix and the dev kit as fallbacks
        std::string bootDir;
        Dl_info info{};
        if (dladdr((void*)&init, &info) && info.dli_fname)
            bootDir = std::filesystem::path(info.dli_fname).parent_path().string();
        const std::string home = getenv("HOME") ? getenv("HOME") : "";
        const std::vector<std::string> bootDirs = {"/home/chris/GITE/chez-pic", bootDir, home + "/.local/lib/hyprscheme", "/usr/lib/hyprscheme"};
        const auto tryBoot = [&bootDirs](const char* name) {
            for (const auto& dir : bootDirs) {
                if (dir.empty())
                    continue;
                const auto p = dir + "/" + name;
                if (std::filesystem::exists(p))
                    return p;
            }
            return std::string(name);
        };
        Sregister_boot_file(tryBoot("petite.boot").c_str());
        Sregister_boot_file(tryBoot("scheme.boot").c_str());
        Sbuild_heap(nullptr, nullptr);

        Sregister_symbol("hl-scheme-bind", (void*)hlSchemeBind);
        Sregister_symbol("hl-scheme-exec", (void*)hlSchemeExec);
        Sregister_symbol("hl-scheme-timer", (void*)hlSchemeTimer);
        Sregister_symbol("hl-scheme-active-title", (void*)hlSchemeActiveTitle);
        Sregister_symbol("hl-scheme-workspace-names", (void*)hlSchemeWorkspaceNames);
        Sregister_symbol("hl-scheme-submap-listen", (void*)hlSchemeSubmapListen);
        Sregister_symbol("hl-scheme-active-window-id", (void*)hlSchemeActiveWindowId);
        Sregister_symbol("hl-scheme-window-ids", (void*)hlSchemeWindowIds);
        Sregister_symbol("hl-scheme-window-title", (void*)hlSchemeWindowTitle);
        Sregister_symbol("hl-scheme-window-alive", (void*)hlSchemeWindowAlive);
        Sregister_symbol("hl-scheme-window-close", (void*)hlSchemeWindowClose);
        Sregister_symbol("hl-scheme-window-class", (void*)hlSchemeWindowClass);
        Sregister_symbol("hl-scheme-window-workspace-name", (void*)hlSchemeWindowWorkspaceName);
        Sregister_symbol("hl-scheme-window-monitor-name", (void*)hlSchemeWindowMonitorName);
        Sregister_symbol("hl-scheme-window-floating", (void*)hlSchemeWindowFloating);
        Sregister_symbol("hl-scheme-window-size", (void*)hlSchemeWindowSize);
        Sregister_symbol("hl-scheme-window-pid", (void*)hlSchemeWindowPid);
        Sregister_symbol("hl-scheme-window-focus", (void*)hlSchemeWindowFocus);
        Sregister_symbol("hl-scheme-window-float", (void*)hlSchemeWindowFloat);
        Sregister_symbol("hl-scheme-window-move-to-workspace", (void*)hlSchemeWindowMoveToWorkspace);
        Sregister_symbol("hl-scheme-monitor-names", (void*)hlSchemeMonitorNames);
        Sregister_symbol("hl-scheme-window-event-listen", (void*)hlSchemeWindowEventListen);
        Sregister_symbol("hl-scheme-window-minimize-listen", (void*)hlSchemeWindowMinimizeListen);
        Sregister_symbol("hl-scheme-lifecycle-listen", (void*)hlSchemeLifecycleListen);
        Sregister_symbol("hl-scheme-config-reloaded-listen", (void*)hlSchemeConfigReloadedListen);
        Sregister_symbol("hl-scheme-unbind", (void*)hlSchemeUnbind);
        Sregister_symbol("hl-scheme-window-same", (void*)hlSchemeWindowSame);
        Sregister_symbol("hl-scheme-current-submap", (void*)hlSchemeCurrentSubmap);
        Sregister_symbol("hl-scheme-cursor-pos", (void*)hlSchemeCursorPos);
        Sregister_symbol("hl-scheme-workspace-active-listen", (void*)hlSchemeWorkspaceActiveListen);
        Sregister_symbol("hl-scheme-window-fullscreen-toggle", (void*)hlSchemeWindowFullscreenToggle);
        Sregister_symbol("hl-scheme-window-fullscreen-mode", (void*)hlSchemeWindowFullscreenMode);
        Sregister_symbol("hl-scheme-focus-workspace", (void*)hlSchemeFocusWorkspace);
        Sregister_symbol("hl-scheme-window-float-act", (void*)hlSchemeWindowFloatAct);
        Sregister_symbol("hl-scheme-focus-direction", (void*)hlSchemeFocusDirection);
        Sregister_symbol("hl-scheme-focus-monitor", (void*)hlSchemeFocusMonitor);
        Sregister_symbol("hl-scheme-focus-last", (void*)hlSchemeFocusLast);
        Sregister_symbol("hl-scheme-focus-urgent", (void*)hlSchemeFocusUrgent);
        Sregister_symbol("hl-scheme-window-move-direction", (void*)hlSchemeWindowMoveDirection);
        Sregister_symbol("hl-scheme-window-swap-direction", (void*)hlSchemeWindowSwapDirection);
        Sregister_symbol("hl-scheme-window-swap-next", (void*)hlSchemeWindowSwapNext);
        Sregister_symbol("hl-scheme-window-swap-with", (void*)hlSchemeWindowSwapWith);
        Sregister_symbol("hl-scheme-window-cycle", (void*)hlSchemeWindowCycle);
        Sregister_symbol("hl-scheme-window-center", (void*)hlSchemeWindowCenter);
        Sregister_symbol("hl-scheme-window-resize-px", (void*)hlSchemeWindowResizePx);
        Sregister_symbol("hl-scheme-window-move-px", (void*)hlSchemeWindowMovePx);
        Sregister_symbol("hl-scheme-window-pin-act", (void*)hlSchemeWindowPinAct);
        Sregister_symbol("hl-scheme-window-pseudo", (void*)hlSchemeWindowPseudo);
        Sregister_symbol("hl-scheme-window-kill", (void*)hlSchemeWindowKill);
        Sregister_symbol("hl-scheme-window-signal", (void*)hlSchemeWindowSignal);
        Sregister_symbol("hl-scheme-window-zorder", (void*)hlSchemeWindowZOrder);
        Sregister_symbol("hl-scheme-window-set-prop", (void*)hlSchemeWindowSetProp);
        Sregister_symbol("hl-scheme-window-tag", (void*)hlSchemeWindowTag);
        Sregister_symbol("hl-scheme-window-clear-tags", (void*)hlSchemeWindowClearTags);
        Sregister_symbol("hl-scheme-toggle-swallow", (void*)hlSchemeToggleSwallow);
        Sregister_symbol("hl-scheme-group-toggle", (void*)hlSchemeGroupToggle);
        Sregister_symbol("hl-scheme-group-cycle", (void*)hlSchemeGroupCycle);
        Sregister_symbol("hl-scheme-group-index", (void*)hlSchemeGroupIndex);
        Sregister_symbol("hl-scheme-group-move-window", (void*)hlSchemeGroupMoveWindow);
        Sregister_symbol("hl-scheme-group-lock", (void*)hlSchemeGroupLock);
        Sregister_symbol("hl-scheme-group-lock-active", (void*)hlSchemeGroupLockActive);
        Sregister_symbol("hl-scheme-window-into-group", (void*)hlSchemeWindowIntoGroup);
        Sregister_symbol("hl-scheme-window-out-of-group", (void*)hlSchemeWindowOutOfGroup);
        Sregister_symbol("hl-scheme-window-into-or-create-group", (void*)hlSchemeWindowIntoOrCreateGroup);
        Sregister_symbol("hl-scheme-window-deny-from-group", (void*)hlSchemeWindowDenyFromGroup);
        Sregister_symbol("hl-scheme-workspace-rename", (void*)hlSchemeWorkspaceRename);
        Sregister_symbol("hl-scheme-workspace-move-monitor", (void*)hlSchemeWorkspaceMoveMonitor);
        Sregister_symbol("hl-scheme-workspace-toggle-special", (void*)hlSchemeWorkspaceToggleSpecial);
        Sregister_symbol("hl-scheme-workspace-swap-monitors", (void*)hlSchemeWorkspaceSwapMonitors);
        Sregister_symbol("hl-scheme-cursor-move", (void*)hlSchemeCursorMove);
        Sregister_symbol("hl-scheme-cursor-corner", (void*)hlSchemeCursorCorner);
        Sregister_symbol("hl-scheme-exit", (void*)hlSchemeExit);
        Sregister_symbol("hl-scheme-reload-config", (void*)hlSchemeReloadConfig);
        Sregister_symbol("hl-scheme-force-renderer-reload", (void*)hlSchemeForceRendererReload);
        Sregister_symbol("hl-scheme-dpms", (void*)hlSchemeDpms);
        Sregister_symbol("hl-scheme-force-idle", (void*)hlSchemeForceIdle);
        Sregister_symbol("hl-scheme-global", (void*)hlSchemeGlobal);
        Sregister_symbol("hl-scheme-event", (void*)hlSchemeEvent);
        Sregister_symbol("hl-scheme-pass", (void*)hlSchemePass);
        Sregister_symbol("hl-scheme-send-shortcut", (void*)hlSchemeSendShortcut);
        Sregister_symbol("hl-scheme-send-key-state", (void*)hlSchemeSendKeyState);
        Sregister_symbol("hl-scheme-mouse", (void*)hlSchemeMouse);
        Sregister_symbol("hl-scheme-release-input-capture", (void*)hlSchemeReleaseInputCapture);
        Sregister_symbol("hl-scheme-window-fullscreen-state", (void*)hlSchemeWindowFullscreenState);
        Sregister_symbol("hl-scheme-layout-message", (void*)hlSchemeLayoutMessage);
        Sregister_symbol("hl-scheme-window-hidden", (void*)hlSchemeWindowHidden);
        Sregister_symbol("hl-scheme-window-pinned", (void*)hlSchemeWindowPinned);
        Sregister_symbol("hl-scheme-window-initial-class", (void*)hlSchemeWindowInitialClass);
        Sregister_symbol("hl-scheme-window-initial-title", (void*)hlSchemeWindowInitialTitle);
        Sregister_symbol("hl-scheme-window-x11", (void*)hlSchemeWindowX11);
        Sregister_symbol("hl-scheme-get-submap-ctx", (void*)+[]() -> ptr {
            return Sstring_utf8((g_regSubmap + "\n" + g_regSubmapReset).c_str(), g_regSubmap.size() + 1 + g_regSubmapReset.size());
        });
        Sregister_symbol("hl-scheme-set-submap-ctx", (void*)+[](const char* name, const char* reset) {
            g_regSubmap      = name ? name : "";
            g_regSubmapReset = reset ? reset : "";
        });
        Sregister_symbol("hl-scheme-enter-submap", (void*)+[](const char* name) -> int {
            if (!g_up || !name)
                return -1;
            return Config::Actions::setSubmap(name) ? 0 : -1;
        });
        Layouts::registerSymbols();

        // phase 1: the prelude (verified plumbing, unguarded). foreign-procedure
        // resolves symbols at definition time, so symbols must exist before this.
        Scall1(Stop_level_value(Sstring_to_symbol("load")),
            Sstring(writeSchemeFile("hypr-scheme-prelude.scm", SCHEME_PRELUDE).c_str()));

        // phase 2: the API, loaded through the prelude's guarded hl--load
        const auto BOOT = writeSchemeFile("hypr-scheme-bootstrap.scm", SCHEME_BOOTSTRAP);
        Scall1(Stop_level_value(Sstring_to_symbol("hl--load")), Sstring(BOOT.c_str()));

        if (Stop_level_value(Sstring_to_symbol("hl--ready")) == Sfalse) {
            LOG(Log::ERR, "[scheme] bootstrap failed, scheme scripting disabled");
            return;
        }
        g_up = true;

        // Chez installs its own SIGSEGV/SIGABRT handlers during init,
        // displacing Hyprland's crash reporter. A plugin cannot reach
        // handleUnrecoverableSignal (static), so reimplement its body via
        // the exported CrashReporter::createAndSaveCrash.
        signal(SIGSEGV, schemeCrashHandler);
        signal(SIGABRT, schemeCrashHandler);

        // hyprctl scheme '<forms>' — evaluate scheme in the compositor.
        // direct registration is safe here: verified working (the deferred
        // doLater variant never fired its callback).
        if (g_pEventLoopManager && IPC::Socket1::sock())
            g_schemeIpcCommand = IPC::Socket1::sock()->registerCommand(IPC::Socket1::SCommand{
                .name    = "scheme",
                .match   = IPC::Socket1::COMMAND_MATCH_PREFIX,
                .handler = [](const IPC::Socket1::SRequest& req) {
                    auto code = req.command.substr(req.command.find_first_of(' ') + 1);
                    const ptr r = Scall1(Stop_level_value(Sstring_to_symbol("hl--eval")), Sstring_utf8(code.c_str(), code.size()));
                    std::string out;
                    if (Sstringp(r)) {
                        for (iptr i = 0; i < Sstring_length(r); ++i)
                            out += (char)Sstring_ref(r, i);
                    }
                    return IPC::Socket1::SResponse(out);
                }});
        // watch the file for edits (skipped during --verify: no event loop yet)
        if (g_pEventLoopManager)
            setupWatch();

        // the lua config load clears all binds; (re)load our file once it settles.
        // as a plugin we load AFTER the initial config load, so also load now.
        g_reloadListener = Event::bus()->m_events.config.reloaded.listen([] { reloadScheme(); });

        // lifecycle: dispatch hl-on-start handlers on the session's first
        // render frame (start fires exactly once, after the first preChecks)
        g_lifecycleListeners.emplace_back(Event::bus()->m_events.start.listen([] {
            g_startSeen = true;
            auto pending = std::move(g_pendingStart);
            g_pendingStart.clear();
            for (const auto id : pending)
                fireScheme(id);
        }));
        reloadScheme();
    }
}


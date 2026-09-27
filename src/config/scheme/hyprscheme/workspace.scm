;; hyprscheme/workspace.scm — the workspace family — raw wrappers over the compositor's workspace operations
;;
;; imports (hyprscheme kernel) [the C++ boundary] and (hyprscheme core)
;; [shared Scheme plumbing], and nothing else in the tree: a family
;; never calls another family. Anything that has to compose several API
;; calls lives in (hyprscheme extras).

(define-module (hyprscheme workspace)
  #:export (
           hl-workspaces hl-workspace-focus! hl-workspace-name-set!
           hl-workspace-monitor-set! hl-workspace-id-set!
           hl-workspace-groups hl-workspace-rule-add! hl-active-workspace
           hl-last-workspace hl-workspace-windows hl-workspace-name
           hl-workspace-addressable-name hl-workspace-number
           hl-workspace-monitor hl-workspace-special?
           hl-workspace-active? hl-workspace-visible? hl-workspace-empty?
           hl-workspace-persistent? hl-workspace-has-urgent?
           hl-workspace-has-fullscreen? hl-workspace-fullscreen-mode
           hl-workspace-fullscreen-window hl-workspace-last-window
           hl-workspace-window-count hl-workspace-group-count
           hl-workspace-tiled-layout hl-workspace-alive? hl-workspace=?
           hl-workspace-active-notification-add!
           hl-workspace-created-notification-add!
           hl-workspace-removed-notification-add!
           hl-workspace-special-active-notification-add! hl-workspace-from
           hl-workspace-special-toggle!
           hl-workspace-move-to-monitor-notification-add!))

(use-modules (hyprscheme kernel))
(use-modules (hyprscheme core))

(use-modules (ice-9 exceptions)
             (rnrs io ports)
             (rnrs lists)
             (srfi srfi-1)
             (ice-9 optargs))

(define (hl-workspaces )
  "All workspaces, as a list of workspace handles."
  (or (hl--c-workspace-names) '()))



;; handles are opaque records whose single field is a guardian CELL
;; ((address . 0) — see the mint helpers and the C++ IHandle comment): the
;; address is a heap weak ref C++-side, and the record's death deletes it
;; through the guardian. Stale handles simply report #f from every getter;
;; the address is the only thing crossing FFI.
;;
;; Each one is built on Guile's own record primitives, with a TYPE-CHECKING
;; accessor: these records are handed to user code, so a handle of the wrong
;; family — or a forged value — must be a clean error rather than a
;; reinterpreted pointer.

(define (hl-workspace-focus! ws)
  "Focus the workspace WS."
  (= 0 (hl--c-focus-workspace (hl--ws-arg ws))))

(define (hl-workspace-name-set! old new)
  "Rename the workspace OLD to NEW."
  (= 0 (hl--c-workspace-rename old (hl--str new))))

(define (hl-workspace-monitor-set! ws mon)
  "Move the workspace WS to monitor MON."
  (= 0 (hl--c-workspace-move-monitor ws mon)))

(define (hl-workspace-id-set! ws new-id)
  "Change the numbered workspace WS's ID to NEW-ID. Only numbered
   workspaces can be re-IDed (named/special cannot); the new ID must be
   > 0 and not in use."
  (= 0 (hl--c-workspace-change-id ws (exact->inexact new-id))))

;; ---- actions: cursor and misc -----------------------------------------------

(define (hl-workspace-groups WS)
  "The groups on workspace WS, as a list of group handles."
  (let ((l (hl--c-workspace-groups WS)))
    (if l l '())))

(define* (hl-workspace-rule-add! ws #:key #:allow-other-keys #:rest fields)
  "Add a workspace rule for WS (a selector or handle):
   (hl-workspace-rule-add! \"3\" #:monitor \"DP-1\" #:layout \"master\").
   #:enabled toggles the rule (a rule is enabled unless you pass
   #:enabled #f). Fields as keywords: monitor default persistent
   gaps_in gaps_out float_gaps border_size no_border no_rounding
   decorate no_shadow on_created_empty default_name layout animation
   layout_opts."
  (let ((spec (map (lambda (x) (if (keyword? x) (keyword->symbol x) x)) fields)))
    (if (not (= 0 (hl--c-workspace-rule-begin (hl--ws-arg ws)
                (if (hl--plist-get spec 'enabled #t) 1 0))))
        (hl--error 'hl-workspace-rule-add! "~a" (hl--c-config-last-error))
        (let loop ((rest spec))
          (cond ((null? rest)
                 (if (= 0 (hl--c-workspace-rule-commit))
                     #t
                     (hl--error 'hl-workspace-rule-add! "~a" (hl--c-config-last-error))))
                (else
                 (let* ((tail (hl--plist-cdr rest))
                        (k  (hl--str (car rest)))
                        (v  (car tail)))
                   (cond ((member k (list "workspace" "enabled"))
                          (loop (cdr tail)))
                         ((equal? k "layout_opts")
                          (let oloop ((o v))
                            (cond ((null? o) (loop (cdr tail)))
                                  (else
                                   (let* ((otail (hl--plist-cdr o))
                                          (sv (hl--rule-spec-value (car otail))))
                                     (if (not sv)
                                         (hl--error 'hl-workspace-rule-add! "bad layout_opts value")
                                         (if (= 0 (hl--c-workspace-rule-layout-opt (hl--str (car o)) sv))
                                             (oloop (cdr otail))
                                             (hl--error 'hl-workspace-rule-add! "~a" (hl--c-config-last-error)))))))))
                         ((member k (list "gaps_in" "gaps_out" "float_gaps"))
                          ;; css-gap fields: scalars and per-side plists both
                          ;; go through the gap parser, whatever their type
                          (hl--c-config-begin)
                          (hl--push-val v)
                          (if (= 0 (hl--c-workspace-rule-gap k))
                              (loop (cdr tail))
                              (hl--error 'hl-workspace-rule-add! "~a" (hl--c-config-last-error))))
                         ((string? v)
                          (if (= 0 (hl--c-workspace-rule-str k v))
                              (loop (cdr tail))
                              (hl--error 'hl-workspace-rule-add! "~a" (hl--c-config-last-error))))
                         ((number? v)
                          (if (= 0 (hl--c-workspace-rule-num k (exact->inexact v)))
                              (loop (cdr tail))
                              (hl--error 'hl-workspace-rule-add! "~a" (hl--c-config-last-error))))
                         ((boolean? v)
                          (if (= 0 (hl--c-workspace-rule-bool k (if v 1 0)))
                              (loop (cdr tail))
                              (hl--error 'hl-workspace-rule-add! "~a" (hl--c-config-last-error))))
                         (else (hl--error 'hl-workspace-rule-add! "unsupported value for ~a" k))))))))))

;; ---- queries ------------------------------------------------------------------

(define (hl-active-workspace )
  "The active workspace, as a handle, or #f."
  (hl--c-active-workspace))

(define (hl-last-workspace )
  "The most recently active workspace, as a handle, or #f."
  (hl--c-last-workspace))

(define (hl-workspace-windows ws)
  "The windows on workspace WS (a handle, or a selector like \"3\",
   \"name:foo\", \"special:bar\"), as a list of window handles."
  (let ((s (hl--c-workspace-windows (hl--ws-arg ws))))
    (if (not s)
        '()
        s)))

;; ---- workspace/monitor handle getters ---------------------------------------
;; every getter takes a handle; a stale or dead handle yields #f from every
;; getter, like an expired object in upstream Lua. Getters mirror Lua's
;; workspace/monitor object fields 1:1.

;; handles are accepted anywhere a selector string is: the handle resolves
;; through its canonical selector, like upstream's selector-or-object
;; helpers. A dead handle resolves to "" and the action fails cleanly.

(define (hl-workspace-from sel)
  "The workspace SELECTOR names, as a handle, or #f when none. SELECTOR uses
   the compositor's own grammar: \"+1\"/\"-1\" (relative number), \"e+1\"
   (the next empty one), \"r+1\" (the next to the right), \"previous\",
   \"empty\", \"special:NAME\", \"name:NAME\", or a bare number or name.
   Finds only — it does not create the workspace; the actions
   (hl-workspace-focus! and friends) take a selector directly and create on
   demand."
  (hl--c-workspace-from (hl--str sel)))

(define (hl-workspace-name w)
  "The workspace's name."
  (hl--c-workspace-name w))

(define (hl-workspace-addressable-name w)
  "The workspace's config-addressable name (usable in selectors and
   rules)."
  (hl--c-workspace-addressable-name w))

(define (hl-workspace-number w)
  "The workspace's numbered ID, or #f for named/special workspaces."
  (hl--c-workspace-number w))

(define (hl-workspace-monitor w)
  "The monitor the workspace is on, as a handle, or #f."
  (hl--c-workspace-monitor w))

(define (hl-workspace-special-toggle! w)
  "Toggle the special workspace W on the focused monitor: open it if it is not
   open (created on demand), close it if it is. #t on success, #f on rejection.
   This is the compositor's own operation — the raw one. The on/off convenience
   built on top of it (and on reading the current state) is
   hl-workspace-special-set! in (hyprscheme extras)."
  (= 0 (hl--c-workspace-toggle-special (hl--ws-arg w))))

(define (hl-workspace-special? w)
  "#t when the workspace is a special workspace."
  (eq? (hl--c-workspace-special w) #t))

(define (hl-workspace-active? w)
  "#t when the workspace is the active (or active special) one on its
   monitor."
  (eq? (hl--c-workspace-active w) #t))

(define (hl-workspace-visible? w)
  "#t when the workspace is currently visible."
  (eq? (hl--c-workspace-visible w) #t))

(define (hl-workspace-empty? w)
  "#t when the workspace has no windows."
  (eq? (hl--c-workspace-empty w) #t))

(define (hl-workspace-persistent? w)
  "#t when the workspace is persistent."
  (eq? (hl--c-workspace-persistent w) #t))

(define (hl-workspace-has-urgent? w)
  "#t when some window on the workspace is urgent."
  (eq? (hl--c-workspace-has-urgent w) #t))

(define (hl-workspace-has-fullscreen? w)
  "#t when a window on the workspace is fullscreen."
  (eq? (hl--c-workspace-has-fullscreen w) #t))

(define (hl-workspace-fullscreen-mode w)
  "The workspace's internal fullscreen mode: 0 none, 1 maximized, 2
   fullscreen; -1 when none."
  (hl--c-workspace-fullscreen-mode w))

(define (hl-workspace-fullscreen-window w)
  "The workspace's fullscreen window, as a handle, or #f."
  (hl--c-workspace-fullscreen-window w))

(define (hl-workspace-last-window w)
  "The workspace's most recently focused window, as a handle, or #f."
  (hl--c-workspace-last-window w))

(define (hl-workspace-window-count w)
  "The number of windows on the workspace."
  (hl--c-workspace-window-count w))

(define (hl-workspace-group-count w)
  "The number of groups on the workspace."
  (hl--c-workspace-group-count w))

(define (hl-workspace-tiled-layout w)
  "The tiled layout currently serving the workspace, as a string."
  (hl--c-workspace-tiled-layout w))

(define (hl-workspace-alive? w)
  "#t when the workspace still exists."
  (eq? (hl--c-workspace-alive w) #t))

(define (hl-workspace=? a b)
  "#t when A and B refer to the same workspace."
  (eq? (hl--c-workspace-same a b) #t))

;; -- monitor getters

(define (hl-workspace-active-notification-add! handler)
  "Register a handler that fires when the active workspace changes. The
   handler receives the workspace handle."
  (let ((rec (make-hl-event 'workspace-active handler)))
    (if (= 0 (hl--c-workspace-active-listen rec))
        rec
        (hl--error 'hl-workspace-active-notification-add! "listener rejected, see compositor log"))))

;; handler signature: (lambda (ws) ...) — ws is a workspace handle

(define (hl-workspace-created-notification-add! handler)
  "Register a handler that fires when a workspace is created. The
   handler receives the workspace handle."
  (hl--workspace-listen 0 'workspace-created handler 'hl-workspace-created-notification-add!))

(define (hl-workspace-removed-notification-add! handler)
  "Register a handler that fires when a workspace is removed. The handle
   it passes is BORN DEAD - every getter on it returns #f, exactly like
   an expired object."
  (hl--workspace-listen 1 'workspace-removed handler 'hl-workspace-removed-notification-add!))

(define (hl-workspace-special-active-notification-add! handler)
  "Register a handler that fires when the opened special workspace on a
   monitor changes: (lambda (ws mon) ...) - ws is #f when no special
   workspace is open on that monitor."
  (hl--workspace-listen 2 'workspace-special-active handler 'hl-workspace-special-active-notification-add!))

(define (hl-workspace-move-to-monitor-notification-add! handler)
  "Register a handler that fires when a workspace moves to a different
   monitor: (lambda (ws mon) ...)."
  (hl--workspace-listen 3 'workspace-move-to-monitor handler 'hl-workspace-move-to-monitor-notification-add!))

;; ---- monitor events -----------------------------------------------------------
;; handler signature: (lambda (mon) ...) — mon is a monitor handle


;; hyprscheme/bind.scm — the bind family — raw wrappers over the compositor's bind operations
;;
;; imports (hyprscheme kernel) [the C++ boundary] and (hyprscheme core)
;; [shared Scheme plumbing], and nothing else in the tree: a family
;; never calls another family. Anything that has to compose several API
;; calls lives in (hyprscheme extras).

(define-module (hyprscheme bind)
  #:export (
           ;; WHY THE hl--keymap-* NAMES ARE HERE: the naming convention is what
           ;; marks a name internal (tools/api-names.scm filters hl--* and *-rtd
           ;; out of the public API), while a module only reaches a later
           ;; importer through its export list — so an unexported helper is
           ;; invisible, which is the decay tools/module-audit.py exists to
           ;; catch.
           ;;
           ;; THIS COMMENT SITS INSIDE THE LIST ON PURPOSE. module-audit reads the
           ;; list as text, so a comment between the names is a live test of its
           ;; comment-stripping: with that broken, every word below becomes an
           ;; "exported name" and the tool reports errors in several modules at
           ;; once. Keeping a comment here means `make check` exercises the fix.
           hl-submap-activate! hl-submap-exit! hl-bind-add!
           hl-repeat hl-bind? hl-submap-notification-add! hl-unbind!
           hl-unbind-key! hl-bind-enabled-set! hl-bind-enabled?
           hl-make-keymap hl-keymap? hl-keymap-submap hl-keymap-submap-reset
           hl-keymap-binds hl-keymap-add! hl-keymap-enable! hl-keymap-disable!
           hl-keymap-enabled? hl-keymap-child hl-keymap-marked?
           hl-keymap-mark! define-keymap
           hl--keymap-submap hl--keymap-submap-reset hl--keymap-binds
           hl--keymap-binds-set! hl--keymap-children hl--keymap-children-set!
           hl--keymap-child-set! hl--keymap-marks hl--keymap-marks-set!
           hl--keymap-enabled hl--keymap-enabled-set! hl-keymap-rtd))

(use-modules (hyprscheme kernel))
(use-modules (hyprscheme core))

(use-modules (ice-9 exceptions)
             (rnrs io ports)
             (rnrs lists)
             (srfi srfi-1)
             (ice-9 optargs))

(define (hl-submap-activate! name) "Switch the active submap to NAME
(\"\" or \"reset\" returns to the default map)."
  (= 0 (hl--c-enter-submap name)))

(define (hl-submap-exit! ) "Leave the active submap and return to the
default map."
  (= 0 (hl--c-enter-submap "")))
;; window read-side fields (LuaWindow parity)

;; ---- config: set/get config options ----------------------------------------
;; one maker address per constructor, fetched from its own one-line C
;; accessor — no name strings, no dispatch

;; helpers for the action wrappers: window #f = active; actions 'toggle/'on/'off;
;; directions "l"/"r"/"u"/"d" or the symbols left/right/up/down
;; optional-boolean convention shared by every toggle/set action:
;; 'unset (the #:key default) → 0 (toggle), #t → 1 (on), #f → 2 (off)

(define* (hl-bind-add! bind #:key (submap "") (submap-reset ""))
  "Register a bind object BIND — as (hl-make-bind ...) built it — and return
it. The RECORD IS THE HANDLE: the object you pass in is the object you get
back, and it is what hl-unbind!, hl-bind-enabled-set! and a keymap all take.

  (hl-bind-add! (hl-make-bind (hl-kbd \"s-q\") (lambda () (hl-window-close!))))

Everything INTRINSIC to the binding — the keys, the thunk, the flags, the
device list, the description — is on the record, so hl-make-bind is where the
option keywords live. What hl-bind-add! takes is PLACEMENT:

#:submap is the SUBMAP this bind lives in, \"\" (the default map, always
active) unless you say otherwise; #:submap-reset is the submap to switch to
AFTER this bind's action runs, for a key that should return you from a mode
(see hl-submap-activate!). Both are the bind's OWN properties in the
compositor — a submap is only a name stamped on the binds that carry it, so
naming it here is all it takes for the submap to exist:

  (hl-bind-add! (hl-make-bind (hl-kbd \"left\") resize-left) #:submap \"resize\")
  (hl-bind-add! (hl-make-bind (hl-kbd \"g\") hl-submap-exit!) #:submap \"resize\")

Placement is per REGISTRATION, which is why it is here and not on the record:
one bind object can be registered more than once, and each registration is a
separate bind in the compositor with its own handle.

#:submap-reset is IGNORED when the action itself switches submap, so a thunk
that calls hl-submap-activate! decides for itself."
  (let* ((tokens (hl--bind-tokens bind))
         (rc (hl--c-bind bind
                        tokens
                        (hl--bind-flags bind)
                        (hl--bind-description bind)
                        (hl--bind-devices bind)
                        (if submap submap "")
                        (if submap-reset submap-reset ""))))
    (if (= 0 rc)
        bind
        (error "hl-bind-add!:" (hl--c-config-last-error)))))
;; ---- key specification helpers -----------------------------------------------
;; (hl-kbd "C-M-a")      — emacs syntax → token list for hl-bind-add!
;; (hl-key "SUPER+SHIFT+Q") — lua/hyprland syntax → token list

(define (hl-repeat ms thunk) "Run THUNK every MS milliseconds until
cancelled with (hl-timer-cancel!) or the config reloads. Returns an
hl-timer handle like hl-after's."
  (let ((rec (make-hl-timer ms thunk)))
    (if (= 0 (hl--c-timer rec (inexact->exact ms) 1))
        rec
        (error "hl-repeat: timer rejected, see compositor log — timeout was (ms):" ms))))

(define (hl-bind? x) (and (struct? x) (eq? (struct-vtable x) hl-bind-rtd)))

(define (hl-submap-notification-add! handler)
  "Register a handler that fires when the active submap changes. The
   handler receives the submap name; an empty string means the default
   submap was restored."
  (let ((rec (make-hl-event 'submap handler)))
    (if (= 0 (hl--c-submap-listen rec))
        rec
        (error "hl-submap-notification-add!: listener rejected, see compositor log"))))

;; ---- workspace events ---------------------------------------------------------

(define (hl-unbind! b)
  "Remove a bind: pass back the hl-bind record hl-bind-add! returned."
  (= 0 (hl--c-unbind-rec b)))

(define (hl-unbind-key! tokens)
  "Remove EVERY bind registered on the same key as TOKENS — a token list,
   as (hl-kbd \"s-l\") returns, never a string. A key can carry several
   binds and this removes them all (the coarse removal, matching the
   compositor's own unbind-by-key); for precise removal use hl-unbind!.

   The comparison is the compositor's, on the bind's DISPLAY KEY, and it is
   case- and whitespace-insensitive. That is why the tokens are joined the
   way they are: a bind registered from the Lua config keeps its argument
   verbatim, so its display key is e.g. \"SUPER + comma\", which normalises
   to \"super+comma\" — the same thing a (hl-kbd \"s-,\") bind's derived
   display key normalises to. Both are reachable with this one call."
  (= 0 (hl--c-unbind-key tokens)))

(define (hl-bind-enabled-set! b on)
  "Arm or disarm bind B, keeping it registered. This is what makes a bind
   CONDITIONAL: register it once and enable it only while some predicate
   holds. A DISABLED bind does not match at all, so its key reaches the
   focused application untouched — with the app's own autorepeat, which
   synthetic key injection cannot reproduce. Returns #t on success; #f if B
   is a stale handle (already unbound, or from a previous generation)."
  (= 0 (hl--c-bind-enabled-set b (if on #t #f))))

(define (hl-bind-enabled? b)
  "#t when bind B is currently armed. #f for a stale handle — one that has
   been unbound, or that a reload has already discarded."
  (= 1 (hl--c-bind-enabled b)))

;; ---- keymaps: a bundle of binds you can arm and disarm together -------------
;;
;; A KEYMAP IS A LIST OF BINDS PLUS A SUBMAP NAME, and that is the whole of it.
;; It is not a new compositor concept — the compositor has one current submap
;; and a bind's submap is a string on the bind — it is a way to KEEP the binds
;; together so they can be turned on and off as a group.
;;
;; `submap` is where the binds are PLACED ("" is the default map, always
;; active); `submap-reset` is applied to every bind it registers. `binds` holds
;; the records hl-bind-add! returned, and because hl-bind-add! returns the
;; object it was handed, each of those IS the bind.
;;
;; WHY IT EXISTS: today a config that wants "these keys in the browser, those
;; in the terminal" registers everything in "" and arms and disarms individual
;; binds (see bindings.scm, which does exactly this through
;; hl-bind-enabled-set!). That works and remains supported — a keymap is that
;; pattern NAMED, so the binds can be re-armed as a set instead of one call per
;; bind. Two keymaps that bind the SAME key and are both armed will both fire:
;; the compositor invokes every full match, and permits it, so a keymap
;; inherits that. It is the same freedom a plain pair of binds already has.
;;
;; The record TYPE is made here rather than in the kernel, unlike the other
;; handle records, and the reason is the rule rather than convenience: the
;; kernel must not depend on a family, and a keymap is a bundle OF BINDS, which
;; this family owns. Nothing in C++ names this type, and no gsubr reads it.
;;
;; `children` is an ALIST of submap name -> keymap, and it is what lets a
;; key-sequence walk find the SAME sub-keymap it made before instead of making a
;; second one for the same name. A keymap that is a prefix — "C-x", so that
;; C-x C-f can exist — holds the map named "C-x", and a second C-x binding
;; reaches that same map through here.
;;
;; `marks` is a set of SYMBOLS recording what has already been done FOR THIS
;; KEYMAP, in the keymap itself rather than in a module-level table. The one
;; user is the emacs layer's 'exit (has it been given its s-G) and 'catchall.
;; A mark is not derivable from the binds: a config is free to bind s-G itself,
;; so "does a bind on s-G exist here" would answer yes for a map we never gave
;; an exit to, and we would then leave it without one.
;;
;; `enabled` is the same kind of fact as `marks` — it records what has been
;; DONE to this keymap, here, rather than being re-derived from the binds. It is
;; set by hl-keymap-enable! / hl-keymap-disable! and by nothing else, so it
;; cannot disagree with the group of binds those two calls just walked. Two
;; callers want it: a config asking whether a mode is currently armed, and
;; hl-keymap-attach-to-tag!, which re-evaluates the whole registry whenever the
;; focused window changes and wants to leave the ones that are already right
;; alone.

(define hl-keymap-rtd (make-record-type 'hl-keymap '(submap submap-reset binds children marks enabled)))

(define hl--keymap-submap (record-accessor hl-keymap-rtd 'submap))

(define hl--keymap-submap-reset (record-accessor hl-keymap-rtd 'submap-reset))

(define hl--keymap-binds (record-accessor hl-keymap-rtd 'binds))

;; binds, children and marks are LISTS, so they are replaced rather than mutated
;; in place: a keymap built by define-keymap is a plain value, and one bind
;; added later must not write into a record someone else may be holding.
;; (record-modifier would work — struct-set! is bound — but a setter on a record
;; that is otherwise immutable buys nothing here and costs a mutation to reason
;; about.)
(define hl--keymap-binds-set! (record-modifier hl-keymap-rtd 'binds))

(define hl--keymap-children (record-accessor hl-keymap-rtd 'children))

(define hl--keymap-children-set! (record-modifier hl-keymap-rtd 'children))

(define hl--keymap-marks (record-accessor hl-keymap-rtd 'marks))

(define hl--keymap-marks-set! (record-modifier hl-keymap-rtd 'marks))

;; `enabled` records whether the keymap was last ARMED or DISARMED, and nothing
;; more. It is not a claim about what the compositor is doing, and there is no
;; cheap way to make it one: the bind records it is derived from are the
;; authority, and a bind can be armed or disarmed on its own (hl-bind-enabled-set!)
;; without the keymap being told. So this is what we TOLD it, which is what a
;; caller wants for "is this keymap currently meant to be on?" — and is why
;; hl-keymap-attach-to-tag! can call hl-keymap-enable! on every re-evaluation
;; without disarming and re-arming keymaps that are already right.
(define hl--keymap-enabled (record-accessor hl-keymap-rtd 'enabled))

(define hl--keymap-enabled-set! (record-modifier hl-keymap-rtd 'enabled))

(define (hl-keymap? x) (and (struct? x) (eq? (struct-vtable x) hl-keymap-rtd)))

(define (hl-keymap-submap km)
  "The submap name a keymap's binds are placed in. \"\" is the default map,
which is always active."
  (hl--keymap-submap km))

(define (hl-keymap-submap-reset km)
  "The submap a keymap's binds switch to after their action runs."
  (hl--keymap-submap-reset km))

(define (hl-keymap-binds km)
  "The bind records this keymap holds, in the order they were added. Each IS
the handle hl-bind-add! returned, so (hl-unbind! b) or
(hl-bind-enabled-set! b #f) can be called on any of them directly."
  (hl--keymap-binds km))

(define* (hl-make-keymap #:key (submap "") (submap-reset ""))
  "Make an empty keymap: a named group of binds, placed in SUBMAP and resetting
to SUBMAP-RESET after their actions run. Both default to \"\", the default map.

  (define browser (hl-make-keymap #:submap \"browser\"))

Add binds with hl-keymap-add!, arm and disarm the whole set with
hl-keymap-enable! / hl-keymap-disable!.

A keymap is ENABLED at birth, deliberately: a keymap is normally a group you
build and then arm, and a submap keymap that started disabled would be one
whose binds never fire, with nothing to say why. define-keymap — which is how a
TOP-LEVEL keymap is normally made, and knows that is what it is — starts the
keymap it returns DISABLED, so a mode does not arm itself the moment the config
is read."
  (make-struct/no-tail hl-keymap-rtd (if submap submap "") (if submap-reset submap-reset "") '() '() '() #t))

(define (hl-keymap-enabled? km)
  "Was KM last armed or disarmed — #t after hl-keymap-enable!, #f after
hl-keymap-disable!.

This is what the keymap was TOLD, not a probe of the compositor: it says what it
is currently MEANT to be, so a caller can ask before arming rather than
re-arming blindly. A bind inside the keymap can still be disarmed on its own
with hl-bind-enabled-set!, and nothing here would know — each bind remains the
authority on itself."
  (hl--keymap-enabled km))

(define (hl-keymap-child km name)
  "The sub-keymap of KM named NAME — one made earlier by a key-sequence walk,
or #f if KM has none. This is what lets a second binding on the same prefix
find the keymap the first one built rather than building a rival."
  (let ((hit (assoc name (hl--keymap-children km))))
    (and hit (cdr hit))))

(define (hl--keymap-child-set! km name child)
  "Record CHILD as the sub-keymap NAME of KM."
  (hl--keymap-children-set! km (acons name child (hl--keymap-children km))))

(define (hl-keymap-marked? km mark)
  "True when MARK has been recorded on KM — see the note on the marks field.
A BOOLEAN, not the list tail memq would answer with: this is a predicate and
reads as one at the call site."
  (and (memq mark (hl--keymap-marks km)) #t))

(define (hl-keymap-mark! km mark)
  "Record MARK on KM, so the thing it stands for is not done twice."
  (unless (memq mark (hl--keymap-marks km))
    (hl--keymap-marks-set! km (cons mark (hl--keymap-marks km))))
  #t)

(define* (hl-keymap-add! km bind #:key (submap #f) (submap-reset #f))
  "Register BIND into keymap KM, in one call: the bind is added to the
compositor with the keymap's placement and its handle is recorded, so the
keymap can arm and disarm the group later. Returns the bind handle — the same
record — so it can also be used on its own.

PER-BIND OVERRIDE: #:submap and #:submap-reset replace the keymap's for THIS
bind only. Both default to #f, meaning \"use the keymap's\" — which is why the
defaults are #f rather than \"\", since \"\" is a real submap (the default map)
and is a different request from \"whatever the keymap says\"."
  (let ((b (hl-bind-add! bind
                         #:submap (if submap submap (hl--keymap-submap km))
                         #:submap-reset (if submap-reset
                                            submap-reset
                                            (hl--keymap-submap-reset km)))))
    (hl--keymap-binds-set! km (cons b (hl--keymap-binds km)))
    b))

(define (hl-keymap-enable! km)
  "Arm every bind in KM. Returns #t.

Arming is not the same as REACHING: a bind in a named submap is only eligible
while that submap is the compositor's current one, so a keymap whose submap is
\"browser\" also needs (hl-submap-activate! \"browser\") before its keys do
anything. A keymap in \"\" is always reachable, which is why the default map
needs only this call.

Arming an already-armed keymap is harmless and is not a special case: this is
the one place the keymap's `enabled` flag is set, so the flag and the binds
cannot disagree."
  (for-each (lambda (b) (hl-bind-enabled-set! b #t)) (hl--keymap-binds km))
  (hl--keymap-enabled-set! km #t)
  #t)

(define (hl-keymap-disable! km)
  "Disarm every bind in KM, leaving them registered. A disabled bind does not
match at all, so its key reaches the focused application untouched — with the
app's own autorepeat, which synthetic key injection cannot reproduce. Returns
#t.

This is the whole point of a keymap: the binds stay registered and can be armed
again without rebuilding them."
  (for-each (lambda (b) (hl-bind-enabled-set! b #f)) (hl--keymap-binds km))
  (hl--keymap-enabled-set! km #f)
  #t)

;; define-keymap — a TOP-LEVEL keymap in one form
;;
;;   (define-keymap browser-keymap ""
;;     ((hl-kbd "C-x") (lambda () (hl-exec! "firefox")) #:locked #t)
;;     ((hl-kbd "C-c") (lambda () (hl-exec! "foot"))))
;;
;; NAME is bound to the keymap, SUBMAP is where its binds are placed, and each
;; CLAUSE is (KEYS THUNK OPTIONS...) — exactly the arguments hl-make-bind takes,
;; so the options are the ordinary bind flags (see bind-flags).
;;
;; THE KEYMAP IT MAKES IS DISABLED, unlike hl-make-keymap's. This is the one
;; form that knows it is building a top-level keymap — the kind a config arms
;; when some condition holds — and arming it at read time would make the mode
;; active from the moment the config loaded. A helper that merely builds a
;; keymap cannot know that, so it arms.
;;
;; Arm and disarm with hl-keymap-enable! / hl-keymap-disable!.
;;
;; Documented as a COMMENT because it is a macro: a macro is not a procedure,
;; so the tree's docstring convention (a leading string in a `define`, read
;; back by procedure-documentation) does not apply to it — `define-syntax`
;; takes no docstring, and procedure-documentation on the result raises rather
;; than answering. There is no equivalent surface, so this block is it.
(define-syntax define-keymap
  (syntax-rules ()
    ((_ name submap (keys thunk opts ...) ...)
     (define name
       (let ((km (hl-make-keymap #:submap submap)))
         ;; ONE BIND PER CLAUSE, INLINE. Not (apply hl-make-bind clause): the
         ;; clause is a list of FORMS — (hl-kbd "C-x"), a lambda, option pairs
         ;; — and apply takes VALUES, so that spelling would need an eval per
         ;; clause and would report a bad clause at load time instead of the
         ;; reader naming the line.
         (hl-keymap-add! km (hl-make-bind keys thunk opts ...)) ...
         (hl-keymap-disable! km)
         km)))))


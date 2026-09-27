;; hyprscheme/query.scm — the query family — raw wrappers over the compositor's query operations
;;
;; imports (hyprscheme kernel) [the C++ boundary] and (hyprscheme core)
;; [shared Scheme plumbing], and nothing else in the tree: a family
;; never calls another family. Anything that has to compose several API
;; calls lives in (hyprscheme extras).

(define-module (hyprscheme query)
  #:export (
           hl-active-title hl-active-monitor hl-active-special-workspace
           hl-is-key-down hl-loaded-plugins hl-version))

(use-modules (hyprscheme kernel))
(use-modules (hyprscheme core))

(use-modules (ice-9 exceptions)
             (rnrs io ports)
             (rnrs lists)
             (srfi srfi-1)
             (ice-9 optargs))

(define (hl-active-title ) "Title of the focused window, or #f when no
window has focus."
  (hl--c-active-title))

(define (hl-active-monitor )
  "The focused monitor, as a handle, or #f."
  (hl--c-active-monitor))

(define (hl-active-special-workspace )
  "The currently open special workspace, as a handle, or #f."
  (hl--c-active-special-workspace))

(define (hl-is-key-down key)
  "#t when the key named by the xkb keysym KEY (e.g. \"Return\") is
   currently pressed."
  (= 1 (hl--c-is-key-down key)))

(define (hl-loaded-plugins )
  "The names of the loaded plugins."
  (or (hl--c-loaded-plugins) '()))

(define (hl-version )
  "The compositor's version string."
  (hl--c-version))


;; Minimal scheme config for the test harness. Tests inject their own
;; binds/handlers through hyprctl scheme. Everything the harness needs at
;; startup lives HERE, in scheme — the lua config is only the one-line
;; compositor bootstrap that cannot be done in scheme (the plugin does not
;; exist until it is loaded).
(hl-state-set! 'harness 'loaded)

(hl-bind (kbd "s-q") (lambda () (hl-exec "foot")))

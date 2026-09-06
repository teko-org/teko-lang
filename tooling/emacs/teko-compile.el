;;; teko-compile.el --- Teko build tool integration for Emacs -*- lexical-binding: t; -*-

;; This file configures Emacs' `compile` command (M-x compile) to work with
;; the Teko build system. It adds error regexp patterns that match Teko's
;; diagnostic output format:
;;   file:line:col: message
;;   file: message (when line is unknown)

;; Add Teko error regexp patterns to compilation-error-regexp-alist.
;; This allows Emacs to parse Teko diagnostics and jump to errors.

(defvar teko-error-regexp
  `(teko
    "^teko: \\([^:\n]+\\):\\([0-9]+\\):\\([0-9]+\\): \\(.+\\)$"
    1 2 3 4)
  "Regexp for Teko errors with full location (teko: file:line:col: message).
The build system adds a 'teko: ' prefix to diagnostic output.
Groups: (1) file, (2) line, (3) column, (4) message.")

(defvar teko-error-regexp-file-only
  `(teko-file
    "^teko: \\([^:\n]+\\): \\(.+\\)$"
    1 nil nil nil)
  "Regexp for Teko errors with file only (teko: file: message).
Groups: (1) file, (2) message.
Used when line/column are unknown.")

(eval-after-load 'compile
  '(progn
     (add-to-list 'compilation-error-regexp-alist 'teko)
     (add-to-list 'compilation-error-regexp-alist 'teko-file)
     (add-to-list 'compilation-error-regexp-alist-alist teko-error-regexp)
     (add-to-list 'compilation-error-regexp-alist-alist teko-error-regexp-file-only)))

;; Optional: Set a default compile command for Teko projects.
;; Users can override this by setting compile-command in their project.
(defun teko-set-compile-command ()
  "Set compile command to 'teko build' for Teko projects."
  (setq-local compile-command "teko build"))

;; Hook into teko-mode to auto-set the compile command.
(eval-after-load 'teko-mode
  '(add-hook 'teko-mode-hook 'teko-set-compile-command))

(provide 'teko-compile)
;;; teko-compile.el ends here

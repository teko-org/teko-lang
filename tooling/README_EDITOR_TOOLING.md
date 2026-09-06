# Teko Editor Tooling & Build Integration

This directory contains syntax highlighting and build tool integration for popular editors. Use these to get IDE-like error navigation when developing Teko projects.

## Supported Editors

### Vim / Neovim

**File:** `vim/compiler/teko.vim`

**Activation:**
```vim
:compiler teko
:make
```

Or add to your `.vimrc`:
```vim
" Auto-select Teko compiler for .tks files
autocmd FileType teko compiler teko
```

**How it works:**
- `:compiler teko` sets up Vim's `:make` command to invoke `teko build`
- `:make` runs the build and parses diagnostics
- Use `:cnext` / `:cprev` to jump between errors
- `:clist` shows all errors in a quickfix window

**Customization:**
```vim
" Custom build command (e.g., build specific target)
let g:teko_makeprg = "teko build --target bin"

" Or per-buffer:
let b:teko_makeprg = "cd /path/to/project && teko build"
```

**Error Format:**
Vim's errorformat recognizes:
- `teko: file:line:col: message` (full location)
- `teko: file: message` (file-only, when line is unknown)

### Emacs

**File:** `emacs/teko-compile.el`

**Setup:**
1. Add to your `.emacs` or `init.el`:
   ```elisp
   (load "/path/to/tooling/emacs/teko-compile.el")
   ```

2. Or use `require` if the tooling is in your load-path:
   ```elisp
   (require 'teko-compile)
   ```

**Usage:**
- `M-x compile` → enter `teko build` (or use the auto-configured default)
- `M-x next-error` (C-x `) → jump to next error
- `M-x previous-error` → jump to previous error
- `M-x compilation-mode` → view error buffer

**Error Format:**
Emacs recognizes two patterns via `compilation-error-regexp-alist`:
- `teko: file:line:col: message` (via `teko` entry)
- `teko: file: message` (via `teko-file` entry, when line is unknown)

The module auto-detects when `teko-mode` is active and sets `compile-command` to `teko build`.

### Nano

**File:** `nano/BUILD.md`

Nano lacks built-in error parsing, so diagnostics are navigated manually:

1. Open Nano alongside a terminal (tmux/screen split recommended)
2. Run `teko build` in the terminal
3. Read diagnostics in format: `file:line:col: message`
4. In Nano: `Ctrl+G` to go to line, fix, save (`Ctrl+X`, `y`)
5. Re-run `teko build` to verify

See `nano/BUILD.md` for details.

---

## Teko Error Format (All Editors)

All Teko diagnostics use this canonical format:

```
teko: <file>:<line>:<column>: <message>
```

The `teko: ` prefix is added by the build system's output layer (`src/build/project.tks`).

Examples:
```
teko: src/main.tks:15:3: undefined variable 'count'
teko: src/types.tks:42:10: type mismatch: expected u32, got str
teko: lib/util.tks:7:1: unused import 'os'
```

When line/column are unknown:
```
teko: <file>: <message>
```

**Format specification** (from `src/checker/diagnostics.tks` and `src/build/project.tks`):
- `teko: ` — fixed prefix (always present)
- `<file>` — source file path (omitted if unknown)
- `<line>` — 1-based line number (omitted if unknown)
- `<column>` — 1-based column number (read when line is known)
- `<message>` — diagnostic text

---

## Installation / Load Paths

### Vim/Neovim

**Plugin directory:** `~/.vim/compiler/` (Unix/Linux/macOS) or `~/vimfiles/compiler/` (Windows)

```bash
# Copy Vim compiler plugin
mkdir -p ~/.vim/compiler
cp tooling/vim/compiler/teko.vim ~/.vim/compiler/

# Or create a symlink (recommended for development)
ln -s /path/to/teko-lang/tooling/vim/compiler/teko.vim ~/.vim/compiler/
```

### Emacs

**Load in init file** (e.g., `~/.emacs` or `~/.config/emacs/init.el`):

```elisp
(load "/path/to/teko-lang/tooling/emacs/teko-compile.el")
```

Or add the directory to load-path:
```elisp
(add-to-list 'load-path "/path/to/teko-lang/tooling/emacs")
(require 'teko-compile)
```

### Nano

No special setup required; see `nano/BUILD.md` for terminal workflow.

---

## Configuration & Customization

### Vim/Neovim

```vim
" Set custom make command (optional)
let g:teko_makeprg = "teko build --release"

" Jump to first error after :make
autocmd QuickFixCmdPost [^l]* nested cwindow

" Map keys for convenience
nnoremap <Leader>m :make<CR>
nnoremap <Leader>e :cnext<CR>
nnoremap <Leader>E :cprev<CR>
```

### Emacs

```elisp
;; Auto-set compile command for all Teko buffers
(setq teko-compile-command "teko build --release")

;; Or per-project in .dir-locals.el
((teko-mode . ((compile-command . "teko build --target lib"))))

;; Auto-jump to first error
(setq compilation-auto-jump-to-first-error t)
```

---

## Testing (Manual Verification)

### Vim/Neovim

1. Open a Teko source file:
   ```bash
   nvim src/main.tks
   ```

2. Activate the compiler:
   ```vim
   :compiler teko
   ```

3. Run a build with intentional errors:
   ```vim
   :make
   ```

4. Verify error parsing in quickfix list:
   ```vim
   :clist
   ```

### Emacs

1. Open a Teko source file:
   ```bash
   emacs src/main.tks
   ```

2. Trigger compile mode:
   ```
   M-x compile
   ```

3. Run: `teko build` (default auto-configured)

4. Check error buffer (`*compilation*`) for parsed diagnostics.

### Nano

See terminal workflow in `nano/BUILD.md`.

---

## Limitations & Known Issues

### Vim/Neovim
- If `:make` runs outside the project directory, errors may not resolve to the correct file paths.
- Workaround: `:chdir` to project root before `:make`, or set `b:teko_makeprg` with explicit path.

### Emacs
- Compilation mode requires the compile buffer to remain open to navigate errors.
- The regexp patterns assume file paths do not contain newlines (standard assumption).

### Nano
- No IDE integration; manual workflow required.
- Recommended: Use tmux/screen splits for efficient terminal + editor usage.

---

## See Also

- `README.md` — Teko language overview
- `CONTRIBUTING.md` — Developer setup and guidelines
- `docs/` — Teko documentation

---

## Contributing

To improve editor tooling, see the language generation tools:
- `tooling/vim/` — Vim syntax/grammar generator
- `tooling/emacs/` — Emacs mode generator
- `tooling/nano/` — Nano syntax generator
- `tooling/shared/` — Shared grammar extraction

These are auto-generated from the language spec; edit `src/` or the `.tkp` generator files instead.

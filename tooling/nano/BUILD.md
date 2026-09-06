# Teko Build/Run Workflow in Nano

Nano does not have built-in support for parsing compiler diagnostics (unlike Vim/Emacs), so error navigation must be done manually. However, Teko provides helpful diagnostic output that you can follow.

## Quick Workflow

1. **Edit your `.tks` file in Nano**
   ```bash
   nano src/mymodule.tks
   ```

2. **Open a terminal alongside Nano** (or use a tmux/screen split)
   - Run `teko build` to compile your project
   - Review errors in the output format: `file:line:col: message`

3. **Navigate to errors**
   - Note the line and column number from the error output
   - In Nano, use `Ctrl+G` (Go to line) to jump to the line
   - Move to the column manually using arrow keys

4. **Verify changes**
   - After editing, save the file (`Ctrl+X` → `y`)
   - Run `teko build` again in the terminal to re-check

## Example Session

```bash
# Start Nano and a split terminal
$ teko build
# Output:
# teko: src/mymodule.tks:15:3: undefined variable 'x'
# teko: src/other.tks:42:10: type mismatch

# In Nano, press Ctrl+G to go to line 15
# Fix the issue, save (Ctrl+X, then y)
# Run 'teko build' again to verify
```

## Recommended Setup

- Use `tmux` or `screen` to split your terminal:
  - Left pane: `nano src/mymodule.tks`
  - Right pane: `teko build` (run as needed)

- Or use a file manager with Nano + terminal:
  - Top: Nano for editing
  - Bottom: Terminal for running `teko build`

## Teko Error Format

All Teko diagnostics follow this format:
```
teko: <file>:<line>:<column>: <message>
```

Where:
- `teko: ` — fixed prefix added by the build system
- `<file>` — source file path (relative or absolute)
- `<line>` — 1-based line number
- `<column>` — 1-based column number (cursor position)
- `<message>` — error or warning text

Examples:
```
teko: src/main.tks:10:5: undefined function 'foo'
teko: src/types.tks:23:1: type mismatch: expected u32, got str
```

When line/column are unknown, the format is:
```
teko: <file>: <message>
```

## See Also

- Teko Build Guide: `docs/build.md` (if available)
- Teko README: `README.md`

For Vim/Neovim users: See `tooling/vim/compiler/teko.vim` for `:make` integration.
For Emacs users: See `tooling/emacs/teko-compile.el` for `M-x compile` integration.

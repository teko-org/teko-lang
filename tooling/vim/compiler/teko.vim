" tooling/vim/compiler/teko.vim — Teko build tool integration for Vim/Neovim
" Usage: :compiler teko
" Then run: :make or :make <target>
"
" This compiler plugin configures Vim's :make command to work with the Teko
" build system. It parses error and warning output in the format:
"   file:line:col: message
"
" The errorformat below matches Teko's diagnostic output (teko::checker::diag_at).

if exists("current_compiler")
  finish
endif
let current_compiler = "teko"

" Allow users to provide custom make settings via b:teko_makeprg or g:teko_makeprg
if exists("b:teko_makeprg")
  let &makeprg = b:teko_makeprg
elseif exists("g:teko_makeprg")
  let &makeprg = g:teko_makeprg
else
  " Default: run 'teko build' in the current/project directory
  let &makeprg = "teko build"
endif

" Teko error format: teko: <file>:<line>:<col>: <message>
" The build system adds a "teko: " prefix to all diagnostic output.
" Split into two patterns:
" 1. Full location (teko: file:line:col: message) — the normal case
" 2. File-only location (teko: file: message) — when line is unknown
let &errorformat = 'teko: %f:%l:%c: %m,teko: %f: %m'

" Optional: Customize how warnings vs. errors are distinguished.
" If Teko includes "warning:" or "error:" in its message (not currently the case),
" we could add a typefilter here. For now, all are treated as errors by default.

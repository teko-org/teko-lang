# Pitfalls

Each of these was paid for once. They are written as rules, with the symptom that leads
back to them — the symptoms are what make them findable, because most of these fail
**silently** or point somewhere else.

## Parsing and the tree

**A generated declaration restores the name of the one being parsed.**
`top_add` clears `p_decl_name()`, and restoring once at the end of a group does not work:
each further `top_add` clears it again. Use `tk_top_emit`, or bracket the whole group.
*Symptom:* a construct that compiles alone crashes when something else is written **before**
it in the same body — a module table keyed by the owner reads through a null name.

**A handler that reads a body itself says whose it is.** An arrow accessor is not read by
the core's function parser, so its statements belong to nobody until the handler calls
`p_set_decl_name`. *Symptom:* the same crash, with no generated declaration in sight.

**`node_assign` copies `nd_next`.** Save it and put it back, or a node rewritten in the
middle of an argument list truncates the list. *Symptom:* arguments disappear after the
rewritten one.

**The registration goes on the node the shaper returns.** A cast that **declares** a
`callp`'s return type only counts when it sits directly on it; any node inserted between
breaks the match in silence. *Symptom:* a float return reads as an integer, or a counted
value is never released. [nodes-and-xt.md](nodes-and-xt.md) has both halves.

**`type_new`, never `type_alias`, for a declared type.** `type_alias("Point", TY_UPTR)`
collapses the identity into `uptr`, and `.` then resolves a member by bare **name**, across
types that have nothing to do with each other. `type_new(name, 8, 8, TK_INT)` keeps the
identity and costs nothing in ABI terms. *Symptom:* a field of an unrelated type resolves
on the wrong receiver.

**`p_start()` is not a pointer into the source after a substitution.** A generic
instantiation replaces the token's text with a lexeme in the arena. A handler that needs to
peek past the current token uses `p_cp()`. *Symptom:* a lookahead scan reads garbage.

**A lookup with a fallback never calls itself on a string it built.** Split the pure scan
out (`tk_struct_find_exact`) and let only the site that reads what the **source** wrote take
the fallback path; a candidate the fallback builds is always longer than the last, so the
recursion never converges. *Symptom:* a segfault in the middle of the parse, with no
message, the two functions alternating on the stack.

**A core prefix never sees a module's postfix.** `- ! ~` are in the core's own prefix
table, and it reads their operand by a recursion that never reaches the point where
`syntax_expr` lives. Registering `syntax_expr("-")` is unreachable code. The fix belongs to
the **postfix** side: `.`/`[` sink through the `- ! ~` chain they received as their left
operand and rebuild it around the result ([`teko_prefix.tk`](../../teko_prefix.tk)).
*Symptom:* `!b[1]` parses as `(!b)[1]`.

**A bare `break;` arrives with level 1, a bare `continue;` with level 0.** The two are not
symmetric in the core, and a lowering that treats them alike is wrong on one of them.
*Symptom:* a `continue` in a lowered `for` escapes one wrapper too few, or too many.

**An `on_source` handler must not push a source.** The frame is already on the lexer's stack
when the callback runs; a push from inside it is refused by the core, and without that guard
it would recurse to a fault. A pre-parse scan reads bytes and nothing else; the push a
construct needs happens in **that construct's** handler.

**A taught word is reserved program-wide unless `source_claim` says otherwise.** The claim
answers by **suffix** — a name ending in `.tk` — plus the frames teko pushes itself, which
carry a description rather than a file name and need the flag `tk_push_source` raises. Get
it wrong in either direction and either mc's own `.mc` sources stop parsing (`type`, `out`
and `params` are parameter names there) or a replayed generic is read with the core's
vocabulary.

**A pre-parse scan asks `lex_file()`, not `p_file()`.** The scan runs before the first
token, so `p_file()` still answers 0 — or, after an include pushes a frame, answers for the
**including** file. *Symptom:* a null pointer where a file name was expected.

**A table of "the most recent declaration of this name" does not answer for a parameter.**
In a pass, ask the declaration being walked — the parameter list, then the body's own
declarations — and answer `-1` when it does not know. Never guess. *Symptom:* a correct
program is refused because a local of the same name, declared in another function, was
found.

## Types, widths and registers

**A parameter that carries an address is declared with pointer width.** The core generates
the slot's load and store from the declared type, so a `ref u8` declared as `u8` **truncates
the pointer on entry**. The slot is `uptr` and the logical type lives in a side table, read
through one pair of accessors (`tk_param_ty`/`tk_decl_param_ty`) so nobody reads `nd_type`
off a parameter again. It holds for a **generated** parameter too — a thunk is built after
the pass that would have fixed it. *Symptom:* compiles clean, faults at run time; a fixture
of width 8 proves nothing about widths 1, 2 and 4.

**A float is decided by kind before width, and declared float in every slot it crosses.**
Choosing `ld64`/`st64` by width moves the right eight bytes into the wrong register file,
and passing an `f64` to an `i64` parameter passes nothing at all. Worse: with both mistakes
at once the program can be **right by accident**, reading the float register the callee left
behind. *Probe that separates the two:* overwrite the variable after capturing it and read
only then, and test the value inside a larger expression so the destination register
changes.

**N parts never become N parameters.** A generated function with one parameter per item of a
variable-length list hits the ABI's twelve-parameter ceiling before any cap the module
believes in — and the message names a file the program never wrote. The data goes into
**memory** instead, written by a chain of calls that hands the block back
([runtime.md](runtime.md)).

**A table sized for a fixture does not survive self-hosting.** The self-hosted unit brings
thousands of lines of core with it. Raise a cap against a **counted** number over this tree,
not by eye. *Symptom:* one clear "too many …" message at a time, each further along the
compile than the last.

## Configs, links and platforms

**The `--config` path stays relative, with no leading `./`, at the package root.** Every
path a config names is resolved against the **config's own** directory, and that directory
is also the project root the `internal` guard measures from. `cfg/teko.toml` cannot find
`core_teko.mc`; `./teko.toml` leaves the guard blind.

**Compile a fixture with `mc build DIR --config FILE`, never `--exe`.** `--exe` always emits
Mach-O, ignoring host and `[target]`. *Symptom:* `Exec format error`, exit 126, off macOS.

**A derived config inherits nothing from the CI matrix.** Whatever a leg carries in
`target_tail` or in its `[linker]` block, a script that derives its own configs has to
derive too ([bootstrap.md](bootstrap.md)).

**On Linux, name the loader this machine has.** mc's ELF writer defaults `interp` and the
libc soname to musl. *Symptom:* `<binary>: not found` with the binary right there, exit 127
— that is the loader speaking.

**On Windows the object is `<name>.exe.o`.** `mc` derives the object from `[project].out` by
appending `.o`, and `out` there ends in `.exe`.

**The taught compiler's `.exe` suffix comes from the host, not from `[target]`.** It has to
run on the machine that wrote it. A ladder therefore refuses a target that is not this
machine rather than guessing suffixes.

**A fixture declares only an `extern` the five pairs all resolve.** Windows has no C
runtime: the link is `-nodefaultlib` plus kernel32, and what answers POSIX names is mc's own
system layer, a fixed list of about fifteen. An `extern` that is **declared and never
called** costs nothing — only a referenced symbol is emitted undefined. *Symptom:*
`undefined symbol: getpid` on the two Windows legs and nowhere else.

**A CI step that accumulates failures needs `set +e`.** The runner's shell already runs with
`-e`, so the first failing fixture ends the step and hides the state of the rest.

## Process

**Never run `git config user.*`.** Identity belongs to the machine, not to a working tree,
and a commit written under a rewritten identity cannot be undone by another commit.

**Read the branch's own CI before draining it anywhere.** Draining first and looking after
has already left two canonical branches red at once.

**A change that does not change accepted code has an identical `--dump-ast`.** That is the
proof a refactor is a no-op, and the first thing to run when one is claimed.

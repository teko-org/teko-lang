# Control flow

`mc`'s core brings `if`, `loop`, `break N` and `continue N`; teko adds C#'s `while`,
`do ... while`, `for`, `foreach`, both spellings of `switch` and the ternary. The core's
forms are not replaced — `loop { }` stays exactly what it is.

## Loops

| written | means |
|---|---|
| `loop { }` | unconditional; leave it with a `break` |
| `while (c) stmt` / `do stmt while (c);` | the body may be a block or a single statement |
| `for (init; cond; step) stmt` | the step is `i++`, `i--`, `i += e`, `i -= e` or `i = e` |
| `for (;;) stmt` | runs until a `break` |
| `foreach (T x in xs) stmt` | a `T[]`, a local fixed array or an inline array field |

`break N` leaves N enclosing loops at once and `continue N` restarts the N-th one, so no
labels are needed; a level beyond the current depth is refused where it is written. A
`continue` inside a `do` lands on the condition and inside a `for` it runs the step, so a
loop that would otherwise never advance still terminates.

A `{ }` block is a scope: a name declared inside it stops answering at the `}`, where a
local of counted type is released ([memory.md](../reference/memory.md)).

## `switch`, twice

The **statement** reads its subject once and takes constant labels — an integer literal or
a `const` — optionally guarded by `when`. Labels that share a body are written one after
the other; a non-empty body may not fall out, so it ends with `break`, `break N`,
`continue` or `return`. `default` may stand anywhere and is tried last. Inside a switch,
`break` leaves the switch, `break 2` also leaves the loop around it, and `continue`
reaches that loop.

The **expression** is `e switch { K => a, K or L => b, _ when c => d, _ => z }`: `or`
joins the labels of one arm, `when` guards an arm, and a `_` arm is required — it is the
unconditional base of the chain, so it cannot itself carry a `when`.

## The ternary

`c ? a : b` has C#'s precedence and associates to the right, so `c1 ? a : c2 ? b : d`
chains. Only the taken arm is evaluated, the two arms must have the same type, and a
ternary of two objects picks between references without allocating. `scope { }` is a
lexical scope of its own, for dependency injection ([di.md](../reference/di.md)).

## One program

```teko
// expect-exit: 42
#include "rt.tk"

const i64 THREE = 3;

i64 classify(i64 n) {
    i64 r = 0;
    switch (n) {
        case 1:
            r = 10;
            break;
        case 2:
        case THREE:                       // a const label, sharing a body
            r = 20;
            break;
        case 10 when n > 5:               // a guarded label
            r = 30;
            break;
        default:
            r = 99;
            break;
    }
    return r;
}

i64 pick(i64 v) {
    return v switch { 1 => 100, 2 or 3 => 200, _ when v > 100 => 999, _ => 0 };
}

i64 main() {
    i64 sum = 0;

    i64 i = 0;
    loop {                                // the core's own unconditional loop
        i = i + 1;
        if (i == 3) break;
    }
    while (i < 5) i = i + 1;              // a single-statement body

    for (i64 m = 0; m < 5; m++) {
        if (m == 2) continue;             // the step still runs
        sum = sum + 1;
    }

    for (i64 a = 0; a < 3; a++) {
        for (i64 b = 0; b < 3; b++) {
            if (b == 1) break 2;          // leaves both loops at once
            sum = sum + 10;
        }
    }

    i64 xs[3];
    xs[0] = 1;
    xs[1] = 2;
    xs[2] = 3;
    foreach (i64 v in xs) {
        sum = sum + v;
    }

    if (classify(THREE) != 20) return 1;
    if (classify(10) != 30) return 2;
    if (classify(7) != 99) return 3;
    if (pick(2) != 200) return 4;
    if (pick(101) != 999) return 5;

    i64 chosen = sum > 10 ? 2 : 0;        // only the taken arm is evaluated
    return i + sum + chosen + 15;
}
```

`match` does not exist (the switch expression covers it) and a bare `when` statement is not
taught. Every form and its limits: [control-flow.md](../reference/control-flow.md).

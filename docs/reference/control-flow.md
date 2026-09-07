# Control flow

`if`, the core's own `loop`/`break N`/`continue N`, C#'s `while`/`do`/`for`/`foreach`, both
spellings of `switch`, the ternary, and `scope { }`.

---

## `if` and blocks

```
if (c) stmt [else stmt]
{ stmt* }
```

A condition is an ordinary expression; a comparison yields `0` or `1`, and `&&`/`||`
short-circuit. A `{ }` block is a scope: a name declared inside it belongs to it and to
nothing else, and it stops answering at the `}` — including for the type of a shadowed
name.

```teko
// expect-exit: 42
#include "rt.tk"

class Shape {
    public i64 area() {
        return 1;
    }
}

class Ledger {
    public i64 area() {
        return 2;
    }
}

i64 report(Shape s) {
    if (1) {
        Ledger s = new Ledger;                   // shadows the parameter, in here only
        if (s.area() != 2) return 0;
    }
    return s.area();                             // Shape's own, again
}

i64 main() {
    Shape sh = new Shape;
    i64 n = 0;
    if (report(sh) == 1) n = 40; else n = 0;
    return n + 2;
}
```

---

## `loop`, `break N`, `continue N`

`loop { }` is the core's own unconditional loop, and it stays: `while` and `for` are
additions, not replacements.

| written | means |
|---|---|
| `break;` | leave the innermost loop |
| `break N;` | leave N enclosing loops at once |
| `continue;` | restart the innermost loop |
| `continue N;` | restart the N-th enclosing loop |

A level beyond the current depth is refused where it is written. No labels are needed: a
level count says everything a label would.

## `while`, `do ... while`, `for`

```
while (c) stmt
do stmt while (c);
for (init; cond; step) stmt
for (;;) stmt
```

The body may be a block or a single statement. A `continue` inside a `do` lands on the
condition, and inside a `for` it runs the step — so a loop that would otherwise never
advance still terminates. `for (;;)` runs until a `break`. The step is a statement:
`i++`, `i--`, `i += e`, `i -= e` and `i = e` are what it may be, and the left side of the
step is a name.

`x++;`, `x--;`, `x += e;` and `x -= e;` are also ordinary statements anywhere.

```teko
// expect-exit: 42
#include "rt.tk"

i64 main() {
    i64 sum = 0;

    i64 i = 0;
    while (i < 3) {
        sum = sum + 1;
        i = i + 1;
    }

    i64 j = 0;
    while (j < 2) j = j + 1;                     // a single-statement body
    sum = sum + j;

    bool done = false;
    i64 spins = 0;
    while (true) {
        spins = spins + 1;
        if (spins == 3) done = true;
        if (done == true) break;
    }
    sum = sum + spins;

    i64 k = 0;
    do {
        k = k + 1;
        if (k == 2) continue;                    // lands on the condition
        sum = sum + 1;
    } while (k < 4);

    for (i64 m = 0; m < 5; m++) {
        if (m == 2) continue;                    // runs the step
        sum = sum + 1;
    }

    for (;;) {
        sum = sum + 1;
        if (sum > 15) break;
    }

    for (i64 a = 0; a < 3; a++) {
        for (i64 b = 0; b < 3; b++) {
            if (b == 1) break 2;                 // leaves both at once
            sum = sum + 10;
        }
    }

    i64 z = 5;
    z++;
    z--;
    z += 3;
    z -= 2;
    return sum + z + 10;                         // 26 + 6 + 10
}
```

---

## `foreach`

```
foreach (T x in source) stmt
```

The source is a **local** `T[]`, a **local** fixed array, or an inline array field reached
through the receiver (`this.items`). The element type may widen (`i64 x in u8[]`), never
narrow. `break`, `break N` and `continue` inside the body behave as in any loop, and the
per-iteration variable only borrows: a counted element is not released by the loop.

```teko
// expect-exit: 42
#include "rt.tk"

class Holder {
    i64[] xs;
    i64 items[4];

    public Holder(i64 n) {
        this.xs = new i64[n];
        for (i64 i = 0; i < this.xs.Length; i++) {
            this.xs[i] = i + 1;
        }
        this.items[0] = 5;
        this.items[3] = 8;
    }

    public i64 sum_all() {
        i64 s = 0;
        foreach (i64 v in this.xs) {
            s = s + v;
        }
        foreach (i64 v in this.items) {          // an inline array field
            s = s + v;
        }
        return s;
    }
}

i64 main() {
    i64 a[4];
    a[0] = 10;
    a[1] = 20;
    a[2] = 30;
    a[3] = 40;
    i64 total = 0;
    foreach (i64 v in a) {
        if (v == 30) continue;
        if (v == 40) break;
        total = total + v;                       // 10 + 20
    }

    u8[] bs = new u8[2];
    bs[0] = 250;
    bs[1] = 5;
    foreach (i64 x in bs) {                      // the element widens
        total = total + x;
    }

    Holder h = new Holder(3);
    return total + h.sum_all() - 262;            // 30 + 255 + 19 - 262
}
```

---

## `switch`, the statement

```
switch (e) {
    case K:
    case L:  stmts break;
    case K when c:  stmts break;
    default: stmts break;
}
```

The subject is read once. A label is a **constant expression** — an integer literal or a
`const` — optionally guarded by `when`. Labels that share a body are written one after the
other; an empty label falls into the next. A non-empty case body **may not fall out**: it
ends with `break`, `break N`, `continue` or `return`. `default` may be written in any
position and is tried last. A duplicate label and a duplicate `default` are refused.

A plain `break` leaves the switch. `break 2` leaves the switch and the loop around it, and
a `continue` written in a case reaches the enclosing loop — a `for`'s step included.

## `switch`, the expression

```
e switch { K => a, K or L => b, _ when c => d, _ => z }
```

The subject is evaluated once. An arm's value is an expression, `or` separates constant
labels of one arm, `when` guards an arm, and a `_` arm is **required** — it is the
unconditional base of the chain. A `when` on the textually **last** `_` arm is refused,
because that arm is never tested.

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
        case THREE:                              // a const label, sharing a body
            r = 20;
            break;
        case 5:
        default:
            r = 99;
            break;
        case 10 when n > 5:
            r = 30;
            break;
    }
    return r;
}

i64 first_hit(i64 target) {
    i64 result = 0 - 1;
    for (i64 i = 0; i < 10; i++) {
        switch (i) {
            case 1:
            case 2:
            case 3:
                if (i == target) {
                    result = i;
                    break 2;                     // out of the switch AND the for
                }
                break;
            default:
                break;
        }
    }
    return result;
}

i64 sum_odd(i64 n) {
    i64 s = 0;
    for (i64 i = 1; i <= n; i++) {
        switch (i % 2) {
            case 0:
                continue;                        // reaches the for's own step
            default:
                break;
        }
        s = s + i;
    }
    return s;
}

i64 pick(i64 v) {
    return v switch { 1 => 100, 2 or 3 => 200, _ when v > 100 => 999, _ => 0 };
}

i64 nested(i64 a, i64 b) {
    return a switch {
        1 => b switch { 1 => 11, _ => 12 },
        _ => 0
    };
}

i64 main() {
    if (classify(1) != 10) return 1;
    if (classify(3) != 20) return 2;
    if (classify(7) != 99) return 3;
    if (classify(10) != 30) return 4;

    if (first_hit(2) != 2) return 5;
    if (sum_odd(6) != 9) return 6;               // 1 + 3 + 5

    if (pick(2) != 200) return 7;
    if (pick(9) != 0) return 8;
    if (pick(101) != 999) return 9;
    if (nested(1, 2) != 12) return 10;

    return classify(1) + pick(2) - nested(1, 1) - 157;   // 10 + 200 - 11 - 157
}
```

---

## The ternary

```
c ? a : b
```

Right-associative, with the precedence C# gives it (below `||`), so `c1 ? a : c2 ? b : d`
chains to the right. Only the taken arm is evaluated. The two arms have the **same type**,
by the compiler's own oracle: mixing them is `teko: the two arms of ?: have different
types`. A ternary of two objects picks between references and allocates nothing; a ternary
of two function names coerces into a delegate ([delegates.md](delegates.md)).

The condition of a `while` may be a ternary and is re-evaluated every pass. A ternary is
not the left side of an assignment.

```teko
// expect-exit: 42
#include "rt.tk"

i64 calls = 0;

i64 counted(i64 v) {
    calls = calls + 1;
    return v;
}

i64 chained(i64 n) {
    return n == 1 ? 10 : n == 2 ? 20 : 30;
}

class Cell {
    public i64 v;

    public Cell(i64 x) {
        v = x;
    }
}

i64 main() {
    i64 x = 1 != 0 ? 42 : 0;
    if (x != 42) return 1;

    if (chained(2) != 20) return 2;
    if (chained(9) != 30) return 3;

    i64 lazy = 1 != 0 ? counted(7) : counted(9);
    if (lazy != 7) return 4;
    if (calls != 1) return 5;                    // only the taken arm ran

    Cell a = new Cell(11);
    Cell b = new Cell(22);
    Cell picked = 0 != 0 ? a : b;
    if (picked.v != 22) return 6;
    if (rt_live() != 2) return 7;                // no object was built to choose

    return x;
}
```

---

## `scope { }`

`scope { ... }` opens a lexical scope for dependency injection: a `IServiceScoped` service
injected inside it is one instance for that block, released at its `}` — including when a
`return` leaves early. It is [di.md](di.md).

---

## Limits

| limit | value |
|---|---|
| a `foreach` over a **parameter**, a **global** or a forward-declared source | not taught |
| a `case` label that is not a constant | refused |
| control falling out of a non-empty `case` | refused |
| a `when` on the textually last `_` arm of a switch expression | refused |
| a switch expression with no `_` arm | refused |
| `match` | not taught: the switch expression covers it |
| a bare `when` statement | not taught: `when` guards a `case` or an arm |
| loops open at one point of one function | 32 |
| case labels | 128 |
| switch expression arms | 64 |

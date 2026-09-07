#!/usr/bin/env python3
"""contrast.py - WCAG 2.1 contrast report for site/static/site.css.

Reads the custom properties declared in the light `:root { ... }` block and in
the `@media (prefers-color-scheme: dark) { :root { ... } }` block of
site/static/site.css, then prints the contrast ratio of every foreground /
background pair the stylesheet actually puts on screen.

    python3 site/tools/contrast.py            # table
    python3 site/tools/contrast.py --markdown # the same table, as Markdown

Copied verbatim from minicompiler/mc's site/tools/contrast.py (MIT), which is
why the pairs below are named after that stylesheet's custom properties: teko's
site.css declares the same ones, with its own colours.

Exit status is 1 when any pair falls below its required minimum, so this can be
wired into a check script later.
"""

import re
import sys
import pathlib

CSS = pathlib.Path(__file__).resolve().parent.parent / "static" / "site.css"

# fg-var, bg-var, what it is, minimum required
PAIRS = [
    ("--text",          "--bg",        "body text",                    4.5),
    ("--text",          "--bg-elev",   "text on a card",               4.5),
    ("--text",          "--code-bg",   "text on a code block",         4.5),
    ("--text-muted",    "--bg",        "muted text, nav link, footer", 4.5),
    ("--text-muted",    "--bg-elev",   "muted text on a card",         4.5),
    ("--accent",        "--bg",        "link",                         4.5),
    ("--accent",        "--bg-elev",   "link on a card",               4.5),
    ("--accent",        "--accent-soft", "active nav item",            4.5),
    ("--accent-hover",  "--bg",        "link, hovered",                4.5),
    ("--bg",            "--accent",    "primary button label",         4.5),
    ("--tok-keyword",   "--code-bg",   "tok-keyword",                  4.5),
    ("--tok-taught",    "--code-bg",   "tok-taught",                   4.5),
    ("--tok-op",        "--code-bg",   "tok-op",                       4.5),
    ("--tok-ident",     "--code-bg",   "tok-ident",                    4.5),
    ("--tok-num",       "--code-bg",   "tok-num",                      4.5),
    ("--tok-str",       "--code-bg",   "tok-str",                      4.5),
    ("--tok-dir",       "--code-bg",   "tok-dir",                      4.5),
    ("--tok-hole",      "--code-bg",   "tok-hole",                     4.5),
    ("--tok-comment",   "--code-bg",   "tok-comment",                  4.5),
    # non-text contrast (WCAG 1.4.11): 3:1 is the requirement
    ("--focus",         "--bg",        "focus ring on the page",       3.0),
    ("--focus",         "--bg-elev",   "focus ring on a card",         3.0),
    ("--focus",         "--code-bg",   "focus ring on a code block",   3.0),
    ("--border-strong", "--bg",        "control border (ghost button)", 3.0),
    ("--border-strong", "--bg-elev",   "control border on a card",     3.0),
    ("--accent",        "--code-bg",   "tok-taught underline",         3.0),
]


def channel(c):
    c = c / 255.0
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def luminance(hexcolor):
    h = hexcolor.lstrip("#")
    if len(h) in (3, 4):                     # #RGB or #RGBA shorthand: expand each nibble
        h = "".join(ch * 2 for ch in h)
    h = h[:6]                                # #RRGGBBAA: the alpha does not enter the luminance
    r, g, b = (int(h[i:i + 2], 16) for i in (0, 2, 4))
    return 0.2126 * channel(r) + 0.7152 * channel(g) + 0.0722 * channel(b)


def ratio(fg, bg):
    a, b = luminance(fg), luminance(bg)
    hi, lo = max(a, b), min(a, b)
    return (hi + 0.05) / (lo + 0.05)


def parse():
    text = CSS.read_text()
    dark_at = text.index("@media (prefers-color-scheme: dark)")
    light_src, dark_src = text[:dark_at], text[dark_at:]
    decl = re.compile(r"(--[a-z0-9-]+)\s*:\s*(#(?:[0-9a-fA-F]{8}|[0-9a-fA-F]{6}|[0-9a-fA-F]{3,4}))\s*;")
    light = dict(decl.findall(light_src))
    dark = dict(light)
    dark.update(dict(decl.findall(dark_src)))
    return light, dark


def main():
    md = "--markdown" in sys.argv
    light, dark = parse()
    failures = 0
    rows = []
    for fg, bg, what, need in PAIRS:
        cells = []
        for theme in (light, dark):
            if fg not in theme or bg not in theme:
                print(f"missing variable: {fg} or {bg}", file=sys.stderr)
                return 2
            r = ratio(theme[fg], theme[bg])
            if r + 1e-9 < need:
                failures += 1
            cells.append((theme[fg], theme[bg], r))
        rows.append((what, fg, bg, need, cells))

    if md:
        print("| What | Foreground / background | Light | Dark | Min |")
        print("|---|---|---:|---:|---:|")
        for what, fg, bg, need, cells in rows:
            l, d = cells
            print(f"| {what} | `{fg}` on `{bg}` | {l[2]:.2f}:1 "
                  f"| {d[2]:.2f}:1 | {need:.1f} |")
    else:
        print(f"{'what':<32} {'light':>22} {'dark':>22}   min")
        print("-" * 84)
        for what, fg, bg, need, cells in rows:
            l, d = cells
            lm = " " if l[2] >= need else "!"
            dm = " " if d[2] >= need else "!"
            print(f"{what:<32} {l[0]}/{l[1]} {l[2]:6.2f}{lm} "
                  f"{d[0]}/{d[1]} {d[2]:6.2f}{dm}  {need:.1f}")
    print(f"\n{len(rows) * 2} pairs checked, {failures} below the minimum",
          file=sys.stderr)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())

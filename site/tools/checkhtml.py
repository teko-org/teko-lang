#!/usr/bin/env python3
"""checkhtml.py - structural and accessibility checks for the rendered pages.

macOS ships HTML Tidy 2006 and libxml2's HTML 4.01 parser; neither knows any
HTML5 element, so both report <header>, <main> and <svg> as invalid and are
useless here. This script checks what those tools would have checked plus the
accessibility rules the design relies on. It is not a substitute for the W3C
Nu validator - run that too if you have it (`vnu site/preview/*.html`).

    python3 site/tools/checkhtml.py site/public/*.html

Copied verbatim from minicompiler/mc's site/tools/checkhtml.py (MIT); mcsite
spawns it from the directory holding site.toml, so it checks this site's pages
against this site's static/.
"""

import sys
import pathlib
from html.parser import HTMLParser

VOID = {"area", "base", "br", "col", "embed", "hr", "img", "input", "link",
        "meta", "param", "source", "track", "wbr"}
# elements that must not nest inside themselves or inside each other
HEADINGS = ["h1", "h2", "h3", "h4", "h5", "h6"]


class Check(HTMLParser):
    def __init__(self, path):
        super().__init__(convert_charrefs=True)
        self.path = path
        self.errors = []
        self.stack = []
        self.ids = {}
        self.label_for = []
        self.href_targets = []
        self.in_svg = 0
        self.headings = []
        self.landmarks = []
        self.title = ""
        self.in_title = False
        self.html_lang = None
        self.anchor_stack = []      # (line, attrs, accumulated text)

    def err(self, msg):
        self.errors.append(f"{self.path}:{self.getpos()[0]}: {msg}")

    def handle_decl(self, decl):
        if self.getpos()[0] == 1 and decl.lower() != "doctype html":
            self.err(f"doctype should be `html`, got `{decl}`")

    def handle_starttag(self, tag, attrs):
        a = dict(attrs)
        if tag == "svg":
            self.in_svg += 1
        if tag not in VOID:
            self.stack.append((tag, self.getpos()[0]))

        if tag == "html":
            self.html_lang = a.get("lang")
        if tag == "title":
            self.in_title = True
        if "id" in a:
            if a["id"] in self.ids:
                self.err(f"duplicate id `{a['id']}` (first at line {self.ids[a['id']]})")
            self.ids[a["id"]] = self.getpos()[0]
        if tag in HEADINGS and not self.in_svg:
            self.headings.append((tag, self.getpos()[0]))
        if tag in ("header", "nav", "main", "footer", "aside"):
            self.landmarks.append(tag)
        if tag == "img" and "alt" not in a:
            self.err("<img> without alt")
        if tag == "label":
            if "for" in a:
                self.label_for.append((a["for"], self.getpos()[0]))
            else:
                self.err("<label> without for=")
        if tag == "input":
            if a.get("type") not in ("hidden", "submit", "button", "reset") \
               and "id" not in a and "aria-label" not in a:
                self.err("<input> with no id to label and no aria-label")
        if tag == "a":
            if "href" not in a:
                self.err("<a> without href")
            elif a["href"].strip() == "":
                self.err("<a> with an empty href")
            else:
                self.href_targets.append((a["href"], self.getpos()[0]))
            self.anchor_stack.append([self.getpos()[0], a, ""])
        # Content-Security-Policy: the site is served with `default-src 'self';
        # script-src 'self'`, which refuses an inline <script>, an inline style
        # attribute and every on*= handler. A page that grows one is broken in
        # production and perfectly fine in a local browser, so it is asserted
        # here rather than discovered by a reader.
        if tag == "script" and "src" not in a:
            self.err("<script> without src= (CSP script-src 'self' forbids inline script)")
        if "style" in a:
            self.err("inline style= attribute (CSP default-src 'self' forbids it)")
        for name in a:
            if name.startswith("on"):
                self.err(f"inline event handler {name}= (CSP forbids it)")
        if tag == "aside" and "aria-label" not in a and "aria-labelledby" not in a:
            self.err("<aside> without an accessible name")
        if tag == "nav" and "aria-label" not in a and "aria-labelledby" not in a:
            self.err("<nav> without an accessible name")
        if "aria-current" in a and a["aria-current"] not in (
                "page", "step", "location", "date", "time", "true", "false"):
            self.err(f"bad aria-current value `{a['aria-current']}`")

    def handle_startendtag(self, tag, attrs):
        self.handle_starttag(tag, attrs)
        if tag not in VOID and self.stack and self.stack[-1][0] == tag:
            self.stack.pop()
        if tag == "svg":
            self.in_svg -= 1

    def handle_endtag(self, tag):
        if tag == "svg":
            self.in_svg -= 1
        if tag == "title":
            self.in_title = False
        if tag in VOID:
            self.err(f"closing tag for the void element </{tag}>")
            return
        if not self.stack:
            self.err(f"stray </{tag}>")
            return
        open_tag, line = self.stack[-1]
        if open_tag != tag:
            self.err(f"</{tag}> closes <{open_tag}> opened at line {line}")
            # resync if the tag is open further up
            for i in range(len(self.stack) - 1, -1, -1):
                if self.stack[i][0] == tag:
                    del self.stack[i:]
                    break
            return
        self.stack.pop()
        if tag == "a" and self.anchor_stack:
            line, a, text = self.anchor_stack.pop()
            named = text.strip() or a.get("aria-label") or a.get("title")
            if not named:
                self.errors.append(
                    f"{self.path}:{line}: <a href=\"{a.get('href','')}\" "
                    "has no accessible name")

    def handle_data(self, data):
        if self.in_title:
            self.title += data
        for frame in self.anchor_stack:
            frame[2] += data

    def finish(self):
        if self.stack:
            for tag, line in self.stack:
                self.err(f"<{tag}> opened at line {line} is never closed")
        if not self.html_lang:
            self.errors.append(f"{self.path}: <html> without lang=")
        if not self.title.strip():
            self.errors.append(f"{self.path}: empty or missing <title>")
        h1s = [h for h in self.headings if h[0] == "h1"]
        if len(h1s) != 1:
            self.errors.append(f"{self.path}: expected exactly one <h1>, found {len(h1s)}")
        prev = 0
        for tag, line in self.headings:
            level = int(tag[1])
            if prev and level > prev + 1:
                self.errors.append(
                    f"{self.path}:{line}: heading level jumps from h{prev} to h{level}")
            prev = level
        for want in ("header", "nav", "main", "footer"):
            if want not in self.landmarks:
                self.errors.append(f"{self.path}: no <{want}> landmark")
        for target, line in self.label_for:
            if target not in self.ids:
                self.errors.append(
                    f"{self.path}:{line}: <label for=\"{target}\"> has no such id")
        for href, line in self.href_targets:
            if href.startswith("#") and len(href) > 1 and href[1:] not in self.ids:
                self.errors.append(
                    f"{self.path}:{line}: fragment link {href} has no target on this page")
        return self.errors


def local_assets(path, root):
    """every /static/... reference must exist on disk"""
    out = []
    text = pathlib.Path(path).read_text()
    for prefix in ('href="/static/', 'src="/static/', 'content="/static/'):
        start = 0
        while (i := text.find(prefix, start)) != -1:
            j = text.index('"', i + len(prefix))
            rel = text[i + len(prefix) - len("static/"):j]
            if not (root / rel).exists():
                out.append(f"{path}: missing asset {rel}")
            start = j
    return out


def main(argv):
    root = pathlib.Path(__file__).resolve().parent.parent
    files = argv[1:] or sorted(str(p) for p in (root / "preview").glob("*.html"))
    bad = 0
    for f in files:
        c = Check(f)
        c.feed(pathlib.Path(f).read_text())
        c.close()
        errs = c.finish() + local_assets(f, root)
        if errs:
            bad += len(errs)
            for e in errs:
                print(e)
        else:
            print(f"{f}: ok")
    print(f"\n{len(files)} files, {bad} problems")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

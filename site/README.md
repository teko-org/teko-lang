# site/ — teko-lang.org

[`docs/`](../docs/README.md) is the source. This directory holds only what turns it into a
website: the configuration, the templates, the stylesheet, the brand assets the pages
reference and the two Python checkers.

**The generator is not here.** `mcsite` is `minicompiler/mc`'s own site generator, written
in mc; [`.github/workflows/site.yml`](../.github/workflows/site.yml) checks that project
out at the tag [`MC_VERSION`](../MC_VERSION) pins, builds `mcsite` there with the pinned
release, and runs it over this directory. Nothing of it is vendored, so the generator can
never drift from the compiler this repository is pinned to. Its full contract — every key
of `site.toml`, every `{{placeholder}}`, the Markdown subset, the `--check` gate — is that
project's own `site/README.md`.

## Looking at the site locally

```sh
git clone --depth 1 --branch "v$(cat MC_VERSION)" https://github.com/minicompiler/mc _mc
(cd _mc && mc build site --config site/mc.toml)         # macOS
# (cd _mc && mc build site --config site/mc.linux.toml) # Linux instead
_mc/build/mcsite site --check                       # renders site/public, then validates
python3 -m http.server 8000 --directory site/public
```

`site/public/` is generated and never committed. `--check` resolves every internal link,
then spawns `tools/checkhtml.py` (structure, accessibility, the Content-Security-Policy
rules) and `tools/contrast.py` (WCAG ratios read out of `static/site.css`) — both from
this directory, so they check this site and not mc's.

## What is here

```
site.toml       the site: title, sections, search, nav_extra
templates/      base.html  page.html  home.html  404.html
static/         site.css  search.js  the mark, the icons and the social card
tools/          checkhtml.py  contrast.py
public/         the built site (generated, not committed)
```

The home page's own prose is [`docs/home-extra.md`](../docs/home-extra.md), not a file of
this directory: `[site] home` is resolved against the documentation root, which also puts
its teko fence under `sh scripts/check-docs.sh` — compiled by the taught compiler and run,
like every other sample on the site.

Everything in `static/` is copied byte for byte to `/static/`. The images there are the
brand's own files ([`docs/brand/`](../docs/brand/README.md)) — `mascot.svg` is the mark in
the header and the hero, `favicon.svg` is the sticker pose, and `social.png` is
`social.svg` rasterised; the generator publishes `static/` and no other directory, which
is why they are copies rather than links.

The templates, the stylesheet, `search.js` and both tools are derived from
`minicompiler/mc`'s own `site/`, which is MIT-licensed; each file says so in its header.

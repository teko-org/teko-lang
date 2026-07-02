# Teko brand guide

The Teko mascot is a **baby guará** (*Eudocimus ruber*, the scarlet ibis), a bird endemic to Brazil. The canonical artwork is [`../teko.svg`](../teko.svg) (the vectorization of the original [`../teko.jpeg`](../teko.jpeg)); every asset here is derived from it, so the mascot is identical everywhere.

## Assets

| File | Use |
|---|---|
| [`logo.svg`](logo.svg) | Horizontal lockup (mascot + wordmark) — README headers, sites, slides |
| [`mascot.svg`](mascot.svg) | The canonical mascot with **transparent background** — general use |
| [`mascot-pastel.svg`](mascot-pastel.svg) | The sober pastel edition (same artwork, recolored) — docs, print, quiet backgrounds |
| [`mascot-lineart.svg`](mascot-lineart.svg) | Ink-only line art — watermarks, print, coloring, engraving |
| [`icon.svg`](icon.svg) | App icon — full mascot on a soft rounded tile (marketplace/app listings) |
| [`poses/hello.svg`](poses/hello.svg) | Speech-bubble "hi!" — welcome pages, onboarding, social posts |
| [`poses/dev.svg`](poses/dev.svg) | Behind a laptop with a `>_` prompt — technical posts, release notes, CLI docs |
| [`poses/sticker.svg`](poses/sticker.svg) | Round sticker/avatar — org avatar, stickers, favicons |
| [`icons/teko-file.svg`](icons/teko-file.svg) | `.tks` source-file icon (VS Code / JetBrains file-type icon) |
| [`icons/teko-file-test.svg`](icons/teko-file-test.svg) | `.tkt` test-file icon (green check badge) |
| [`icons/teko-file-project.svg`](icons/teko-file-project.svg) | `.tkp` manifest icon (blue cube badge) |
| [`icons/teko-file-lib.svg`](icons/teko-file-lib.svg) | `.tkl` package icon (amber package badge) |
| [`icons/teko-mono.svg`](icons/teko-mono.svg) | Single-color glyph (`currentColor`) — toolbars, status bars, dark/light themes |

The file icons are the mascot's face crop with a corner badge; they carry `width/height="32"` and read well from 16 px up. For marketplace submissions export PNG at the sizes each platform asks (VS Code: SVG or 128 px PNG; JetBrains: 16/32/40 px, pair the colored icons with `teko-mono.svg` for dark themes).

## Palettes

### Vivid (canonical — from `teko.svg`)

| Role | Color |
|---|---|
| Guará red (body) | `rgb(245,14,12)` |
| Red shading | `rgb(206,2,2)` · `rgb(146,5,6)` · `rgb(61,2,2)` |
| Red highlights | `rgb(247,52,48)` · `rgb(252,95,95)` |
| Mouth | `rgb(205,28,35)` · `rgb(215,73,72)` |
| Ink / outline / beak | `rgb(5,2,2)` |
| Beak sheen | `rgb(47,46,46)` · `rgb(75,73,74)` · `rgb(146,145,145)` |
| Cheek | `rgb(252,150,3)` |
| Eye white | `rgb(245,245,245)` |
| Original background green | `rgb(10,70,26)` (removed in brand assets) |

### Pastel (sober) edition — the mapping applied in `mascot-pastel.svg`

| Vivid | → Pastel |
|---|---|
| `rgb(245,14,12)` body | `#F2A59C` |
| `rgb(206,2,2)` / `rgb(146,5,6)` / `rgb(61,2,2)` shading | `#DE8C82` / `#C97F76` / `#B0736B` |
| `rgb(247,52,48)` / `rgb(252,95,95)` highlights | `#F5B3AA` / `#F8C6BE` |
| `rgb(205,28,35)` / `rgb(215,73,72)` mouth | `#E8968C` / `#EFA79D` |
| `rgb(5,2,2)` ink | `#5D4A52` |
| `rgb(47,46,46)` / `rgb(75,73,74)` / `rgb(146,145,145)` sheen | `#6E5A64` / `#7C6870` / `#C4B2B8` |
| `rgb(252,150,3)` cheek | `#F4BC7E` |
| `rgb(245,245,245)` eye | `#FFF9F5` |

Support colors: wordmark ink `#2B2027`, soft background `#FDF3EC`, badge green `#3E9E4E`, badge blue `#3D6FA8`, badge amber `#C8891A`.

## Usage rules

- `docs/teko.svg` is the source of truth; regenerate derived assets from it rather than editing them freehand.
- Use the vivid edition for avatars, icons and high-energy marketing; the pastel edition for documentation and print.
- Don't recolor outside the two palettes, don't stretch or add effects (shadows/gradients) to the logo.
- Keep clear space around the logo of at least the height of the "o".
- New poses should compose the canonical artwork with props (as `poses/` does) so the mascot stays identical everywhere.

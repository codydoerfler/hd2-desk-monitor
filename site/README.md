# Project site

A one-page showcase for the desk monitor, built as a single self-contained
HTML file with no external requests — every image and the Anton face are
inlined as `data:` URIs.

## Build

```bash
python3 site/build.py
```

Reads `site/template.html` and writes two outputs:

| File | Purpose |
|---|---|
| `site/index.html` | Complete standalone document. This is the one to host. |
| `site/artifact.html` | Body content only, for a claude.ai artifact publish (that host supplies its own `<head>`). |

Both are generated — edit `site/template.html`, never the outputs.

## Where the content comes from

Nothing on the page is a mockup. The eight screens in the hero bezel and the
three event screens are the PNGs in `docs/`, produced by `tools/preview.sh`,
which compiles the firmware's own renderer on the host and rasterises it. The
palette in the stylesheet is copied from `namespace theme` in `src/config.h`,
and the hero carousel advances on `kCarouselCycleMs` (7 s) — the same beat the
device pages at.

If a HUD screen changes, re-run `./tools/preview.sh` and then `site/build.py`;
the page picks the new renders up automatically.

## Deploy — Cloudflare Workers

The site is hosted as a Cloudflare Worker on the `rrwestminster.com` account,
the same pattern as `bitaxe.rrwestminster.com`.

1. Go to <https://dash.cloudflare.com> → Workers & Pages → Create → **Upload
   your static files**.
2. Upload `site/index.html` (the filename must be `index.html`).
3. Set the Worker name before deploying — e.g. `rrw-hd2-monitor`.
4. Deploy, then open the Worker's **Domains** tab → Add Domain →
   `rrwestminster.com`, subdomain `hd2`.

To publish a new version later, use **New deployment** on the existing Worker
rather than creating a second one, so the public URL does not change.

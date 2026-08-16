#!/usr/bin/env python3
"""Build the project site into a single self-contained HTML file.

Every asset — the Anton face the firmware renders headings in, and each HUD
screen produced by tools/preview.sh — is inlined as a data: URI, so the output
is one file with no external requests. That is what the Cloudflare Workers
static-upload flow wants, and it is what the artifact CSP requires.

    python3 site/build.py

Outputs:
    site/index.html     standalone document, for hosting
    site/artifact.html  body-content only, for a claude.ai artifact publish
"""

import base64
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SITE = ROOT / "site"

# The hero carousel. Order matters — it is the order the bezel pages through.
# Names are the device's own vernacular, read off each render's header.
CAROUSEL = [
    ("liberation", "Liberation"),
    ("invasion",   "Defence under assault"),
    ("count",      "Eradication"),
    ("extraction", "Extraction"),
    ("campaign",   "Campaign feed"),
    ("defense",    "Defence — standing by"),
    ("idle",       "No active order"),
    ("stale",      "Offline / stale"),
]

# Full-panel event screens, placed individually in the markup.
EVENTS = ["neworder", "success", "failure"]

FONT = ROOT / "tools" / "assets" / "Anton-Regular.ttf"


def b64(path: pathlib.Path) -> str:
    if not path.exists():
        sys.exit(f"missing asset: {path.relative_to(ROOT)}")
    return base64.b64encode(path.read_bytes()).decode("ascii")


def preview(name: str) -> pathlib.Path:
    return ROOT / "docs" / f"preview_{name}.png"


def esc(s: str) -> str:
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;").replace('"', "&quot;")


def main() -> None:
    template = (SITE / "template.html").read_text(encoding="utf-8")

    # --- hero carousel ---------------------------------------------------
    imgs = []
    for i, (name, label) in enumerate(CAROUSEL):
        imgs.append(
            '<img src="data:image/png;base64,{data}" '
            'data-name="{label}" '
            'alt="Device HUD: {alt}" '
            'width="960" height="640"{extra}>'.format(
                data=b64(preview(name)),
                label=esc(label),
                alt=esc(label.lower()),
                extra="" if i == 0 else ' loading="lazy"',
            )
        )
    html = template.replace("{{CAROUSEL_IMAGES}}", "\n          ".join(imgs))

    # --- individually placed assets --------------------------------------
    html = html.replace("{{FONT_ANTON}}", b64(FONT))
    for name in EVENTS:
        html = html.replace("{{IMG_%s}}" % name, b64(preview(name)))

    if "{{" in html:
        leftover = html[html.index("{{"):html.index("{{") + 40]
        sys.exit(f"unsubstituted token in template: {leftover!r}")

    # --- artifact variant: body content only -----------------------------
    # The artifact host supplies <!doctype>, <html>, <head> and <body>.
    (SITE / "artifact.html").write_text(html, encoding="utf-8")

    # --- standalone variant: a complete document -------------------------
    standalone = (
        "<!doctype html>\n"
        '<html lang="en">\n'
        "<head>\n"
        '<meta charset="utf-8">\n'
        '<meta name="viewport" content="width=device-width, initial-scale=1">\n'
        '<meta name="description" content="An always-on ESP32 desk display that '
        "shows the live Helldivers 2 Major Order — target planet, liberation "
        'percentage, time remaining and player counts — on a 4-inch 480x320 panel.">\n'
        '<meta name="theme-color" content="#0A0C10">\n'
        '<meta property="og:title" content="Major Order Desk Monitor">\n'
        '<meta property="og:description" content="A Helldivers 2 Major Order, '
        'on your desk. ESP32, 480x320, updates itself over the air.">\n'
        '<meta property="og:type" content="website">\n'
        + html
        + "\n</body>\n</html>\n"
    )
    # The <title>/<style> the template opens with belong in <head>; everything
    # from the frame div onward is body. Split at the first structural div.
    marker = '<div class="frame"'
    head_part, body_part = standalone.split(marker, 1)
    standalone = head_part + "</head>\n<body>\n" + marker + body_part

    (SITE / "index.html").write_text(standalone, encoding="utf-8")

    for f in ("index.html", "artifact.html"):
        size = (SITE / f).stat().st_size
        print(f"  site/{f:16s} {size:>9,} bytes")


if __name__ == "__main__":
    main()

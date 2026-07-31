# Header bar source assets

Inputs to `tools/gen_header_art.py` and `tools/gen_anton_font.py`. Nothing here
is compiled or flashed — the generators turn these into the `src/hud_*.h`
tables, which are what the firmware links. They are checked in so those
generators can be re-run.

| File | What it is | Licence |
| --- | --- | --- |
| `Anton-Regular.ttf` | The Major Order title face. Only U+0020–U+005A is rasterised into `src/hud_font_anton.h`. | SIL Open Font License 1.1 (Google Fonts) |
| `earth_nasa.jpg` | NASA Blue Marble, the globe behind the header text. | Public domain (NASA) |
| `skull_final_*.png` | The badge skull, traced from a game screenshot at three sizes. `gen_header_art.py` uses the 64px cut. | Traced artwork, this repo |
| `skull_traced_mask.png` | Intermediate trace, kept for reference. | Traced artwork, this repo |
| `header_mockup_reference.py` | The approved full-size mockup of the bar. This is the visual spec: the colours, proportions and layout in `gen_header_art.py` and `layout::hdr*` are taken from its `draw_header_bar()`. Needs numpy, which the generators deliberately do not. | This repo |

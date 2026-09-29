"""Read an SVG's <path> elements and rasterise them to a coverage mask.

Written because this machine has no SVG rasteriser — no cairosvg, no librsvg,
no numpy — and the official faction artwork in tools/assets/official_icons/
arrives as SVG. The alternative was macOS' qlmanage, which renders these
correctly but composites onto opaque white (so edge coverage has to be
un-blended back out of the colour) and ties a committed asset pipeline to
whatever WebKit ships that month. This is ~200 lines of Pillow instead, and it
is deterministic: the same SVG gives the same bytes on any machine that can
import PIL.

It is deliberately not a general SVG renderer. It handles what the four source
files use and refuses the rest loudly:

  * <path> only — no <rect>/<circle>/<polygon> shorthand, no <g>, no <use>
  * the full path grammar (MmZzLlHhVvCcSsQqTtAa), including elliptical arcs
  * transform="translate(...) scale(...) matrix(...) rotate(...)" on the
    element or inherited from the root <svg>
  * one flat fill per path, from style="fill:#rrggbb" or fill="#rrggbb";
    fill:none paths are dropped (all four files carry one as a bounding box)

Fill rule is even-odd, which is what the source art declares and what its
holes need — the Helldiver skull's eyes and mouth and the Illuminate's ring
are subpaths drawn inside the outer silhouette, and under nonzero winding they
would fill solid.

Coverage is produced the same way tools/gen_icons.py has always produced it:
draw at an 8x supersample, box-filter down, so a destination pixel's value is
the fraction of it the shape covers. Callers threshold that (1-bit icons) or
cut it at 50% (the RGB565 faction badges, which have no alpha channel).
"""
import math
import re

from PIL import Image, ImageChops, ImageDraw

# Samples per curve segment. These shapes are rasterised into slots of a few
# dozen pixels, so this is far past the point where more would change a
# destination pixel; it is set for the source's scale, not the target's.
CURVE_STEPS = 64

# --------------------------------------------------------------------------
#  Affine transforms, as (a, b, c, d, e, f) == | a c e |
#                                              | b d f |
# --------------------------------------------------------------------------

IDENTITY = (1.0, 0.0, 0.0, 1.0, 0.0, 0.0)

_XFORM = re.compile(r"(matrix|translate|scale|rotate)\s*\(([^)]*)\)")
_NUM = re.compile(r"[-+]?(?:\d*\.\d+|\d+\.?)(?:[eE][-+]?\d+)?")


def mul(m, n):
    """m then n, i.e. apply(mul(m, n), p) == apply(n, apply(m, p))."""
    a1, b1, c1, d1, e1, f1 = m
    a2, b2, c2, d2, e2, f2 = n
    return (a2 * a1 + c2 * b1, b2 * a1 + d2 * b1,
            a2 * c1 + c2 * d1, b2 * c1 + d2 * d1,
            a2 * e1 + c2 * f1 + e2, b2 * e1 + d2 * f1 + f2)


def apply(m, x, y):
    a, b, c, d, e, f = m
    return (a * x + c * y + e, b * x + d * y + f)


def parse_transform(s):
    """SVG transform list -> one matrix. Leftmost is applied last, per spec."""
    if not s:
        return IDENTITY
    out = IDENTITY
    for kind, body in _XFORM.findall(s):
        v = [float(t) for t in _NUM.findall(body)]
        if kind == "matrix":
            m = tuple(v[:6])
        elif kind == "translate":
            m = (1.0, 0.0, 0.0, 1.0, v[0], v[1] if len(v) > 1 else 0.0)
        elif kind == "scale":
            sx = v[0]
            m = (sx, 0.0, 0.0, v[1] if len(v) > 1 else sx, 0.0, 0.0)
        else:  # rotate
            a = math.radians(v[0])
            m = (math.cos(a), math.sin(a), -math.sin(a), math.cos(a), 0.0, 0.0)
        # A transform list reads left to right as outermost to innermost, so
        # each new one is applied *before* those already accumulated.
        out = mul(m, out)
    return out


# --------------------------------------------------------------------------
#  Path data
# --------------------------------------------------------------------------

_CMDS = "MmZzLlHhVvCcSsQqTtAa"


def _tokens(d):
    i, n = 0, len(d)
    while i < n:
        ch = d[i]
        if ch in _CMDS:
            yield ch
            i += 1
        elif ch in " ,\t\r\n":
            i += 1
        else:
            m = _NUM.match(d, i)
            if not m:
                raise ValueError(f"unparsable path data at offset {i}: {d[i:i + 16]!r}")
            yield float(m.group())
            i = m.end()


def _arc(p0, rx, ry, phi, large, sweep, p1, out):
    """Endpoint-parameterised elliptical arc, per SVG 1.1 appendix F.6."""
    x0, y0 = p0
    x1, y1 = p1
    if rx == 0 or ry == 0 or (x0 == x1 and y0 == y1):
        out.append(p1)
        return
    rx, ry = abs(rx), abs(ry)
    cosp, sinp = math.cos(phi), math.sin(phi)

    dx2, dy2 = (x0 - x1) / 2.0, (y0 - y1) / 2.0
    x1p = cosp * dx2 + sinp * dy2
    y1p = -sinp * dx2 + cosp * dy2

    # F.6.6: grow radii that are too small to join the endpoints at all.
    lam = (x1p * x1p) / (rx * rx) + (y1p * y1p) / (ry * ry)
    if lam > 1.0:
        s = math.sqrt(lam)
        rx, ry = rx * s, ry * s

    num = rx * rx * ry * ry - rx * rx * y1p * y1p - ry * ry * x1p * x1p
    den = rx * rx * y1p * y1p + ry * ry * x1p * x1p
    coef = math.sqrt(max(0.0, num / den))
    if large == sweep:
        coef = -coef
    cxp, cyp = coef * rx * y1p / ry, -coef * ry * x1p / rx
    cx = cosp * cxp - sinp * cyp + (x0 + x1) / 2.0
    cy = sinp * cxp + cosp * cyp + (y0 + y1) / 2.0

    def angle(ux, uy, vx, vy):
        dot = ux * vx + uy * vy
        det = ux * vy - uy * vx
        return math.atan2(det, dot)

    th0 = angle(1.0, 0.0, (x1p - cxp) / rx, (y1p - cyp) / ry)
    dth = angle((x1p - cxp) / rx, (y1p - cyp) / ry,
                (-x1p - cxp) / rx, (-y1p - cyp) / ry)
    if not sweep and dth > 0:
        dth -= 2 * math.pi
    elif sweep and dth < 0:
        dth += 2 * math.pi

    steps = max(2, int(CURVE_STEPS * abs(dth) / (2 * math.pi)) + 1)
    for i in range(1, steps + 1):
        th = th0 + dth * i / steps
        ct, st = math.cos(th), math.sin(th)
        out.append((cosp * rx * ct - sinp * ry * st + cx,
                    sinp * rx * ct + cosp * ry * st + cy))


def _cubic(p0, p1, p2, p3, out):
    for i in range(1, CURVE_STEPS + 1):
        t = i / CURVE_STEPS
        u = 1 - t
        a, b, c, d = u * u * u, 3 * u * u * t, 3 * u * t * t, t * t * t
        out.append((a * p0[0] + b * p1[0] + c * p2[0] + d * p3[0],
                    a * p0[1] + b * p1[1] + c * p2[1] + d * p3[1]))


def _quad(p0, p1, p2, out):
    for i in range(1, CURVE_STEPS + 1):
        t = i / CURVE_STEPS
        u = 1 - t
        out.append((u * u * p0[0] + 2 * u * t * p1[0] + t * t * p2[0],
                    u * u * p0[1] + 2 * u * t * p1[1] + t * t * p2[1]))


def flatten(d, matrix=IDENTITY):
    """Path data -> subpaths, each a list of points in the matrix' output space.

    Every subpath comes back closed in the sense that fills treat it as closed,
    which is what SVG does for a fill regardless of whether the data says `z`.
    """
    subs, cur = [], []
    cx = cy = sx = sy = 0.0
    prev_ctrl = None   # reflected control point for S/T
    prev_cmd = None
    cmd = None
    it = _tokens(d)
    pending = []

    def take(n):
        vals = []
        for _ in range(n):
            if pending:
                vals.append(pending.pop(0))
            else:
                v = next(it)
                if isinstance(v, str):
                    raise ValueError(f"command {v!r} where a number was expected")
                vals.append(v)
        return vals

    def flush():
        if len(cur) >= 2:
            subs.append([apply(matrix, x, y) for x, y in cur])

    while True:
        if pending:
            tok = cmd
            # An implicit repeat: M/m repeats as L/l, everything else as itself.
            if tok == "M":
                tok = "L"
            elif tok == "m":
                tok = "l"
        else:
            try:
                tok = next(it)
            except StopIteration:
                break
            if not isinstance(tok, str):
                pending.append(tok)
                if cmd is None:
                    raise ValueError("path data starts with a number")
                tok = "L" if cmd == "M" else ("l" if cmd == "m" else cmd)
            cmd = tok if isinstance(tok, str) else cmd

        rel = tok.islower()
        c = tok.upper()

        if c == "M":
            x, y = take(2)
            if rel:
                x, y = cx + x, cy + y
            flush()
            cur = [(x, y)]
            cx, cy, sx, sy = x, y, x, y
        elif c == "Z":
            flush()
            cur = [(sx, sy)]
            cx, cy = sx, sy
        elif c in ("L", "H", "V"):
            if c == "L":
                x, y = take(2)
                if rel:
                    x, y = cx + x, cy + y
            elif c == "H":
                (x,) = take(1)
                x, y = (cx + x if rel else x), cy
            else:
                (y,) = take(1)
                x, y = cx, (cy + y if rel else y)
            cur.append((x, y))
            cx, cy = x, y
        elif c in ("C", "S"):
            if c == "C":
                x1, y1, x2, y2, x, y = take(6)
                if rel:
                    x1, y1, x2, y2, x, y = (cx + x1, cy + y1, cx + x2, cy + y2,
                                            cx + x, cy + y)
            else:
                x2, y2, x, y = take(4)
                if rel:
                    x2, y2, x, y = cx + x2, cy + y2, cx + x, cy + y
                # Reflect the previous curve's second control point; with no
                # previous curve the spec says use the current point.
                if prev_cmd in ("C", "S") and prev_ctrl:
                    x1, y1 = 2 * cx - prev_ctrl[0], 2 * cy - prev_ctrl[1]
                else:
                    x1, y1 = cx, cy
            _cubic((cx, cy), (x1, y1), (x2, y2), (x, y), cur)
            prev_ctrl = (x2, y2)
            cx, cy = x, y
        elif c in ("Q", "T"):
            if c == "Q":
                x1, y1, x, y = take(4)
                if rel:
                    x1, y1, x, y = cx + x1, cy + y1, cx + x, cy + y
            else:
                x, y = take(2)
                if rel:
                    x, y = cx + x, cy + y
                if prev_cmd in ("Q", "T") and prev_ctrl:
                    x1, y1 = 2 * cx - prev_ctrl[0], 2 * cy - prev_ctrl[1]
                else:
                    x1, y1 = cx, cy
            _quad((cx, cy), (x1, y1), (x, y), cur)
            prev_ctrl = (x1, y1)
            cx, cy = x, y
        elif c == "A":
            rx, ry, rot, large, sweep, x, y = take(7)
            # The compact form packs the two flags against the next number
            # ("a1 1 0 011 1"). None of the source art does, and a tokeniser
            # that reads "011" as one number would rasterise silently wrong, so
            # this refuses rather than guesses.
            if large not in (0.0, 1.0) or sweep not in (0.0, 1.0):
                raise ValueError("arc flags are not 0/1 — packed arc syntax is "
                                 "not supported; space the flags out")
            if rel:
                x, y = cx + x, cy + y
            _arc((cx, cy), rx, ry, math.radians(rot), int(large), int(sweep),
                 (x, y), cur)
            cx, cy = x, y
        else:
            raise ValueError(f"unsupported path command {tok!r}")

        if c not in ("C", "S"):
            if c not in ("Q", "T"):
                prev_ctrl = None
        prev_cmd = c

    flush()
    return subs


# --------------------------------------------------------------------------
#  Documents
# --------------------------------------------------------------------------

_PATH_EL = re.compile(r"<path\b([^>]*)/?>", re.S)
_ATTR = re.compile(r"([\w:-]+)\s*=\s*\"([^\"]*)\"", re.S)
_SVG_EL = re.compile(r"<svg\b([^>]*)>", re.S)
_FILL = re.compile(r"fill\s*:\s*([^;]+)")


def _fill_of(attrs):
    style = attrs.get("style", "")
    m = _FILL.search(style)
    if m:
        return m.group(1).strip()
    return attrs.get("fill", "#000000").strip()


def load(path):
    """An SVG file -> [(subpaths, '#rrggbb')], fill:none paths dropped."""
    with open(path) as f:
        doc = f.read()

    root = _SVG_EL.search(doc)
    root_x = parse_transform(dict(_ATTR.findall(root.group(1))).get("transform", "")
                             if root else "")

    out = []
    for body in _PATH_EL.findall(doc):
        attrs = dict(_ATTR.findall(body))
        if "d" not in attrs:
            continue
        fill = _fill_of(attrs)
        if fill.lower() in ("none", "transparent"):
            continue
        m = mul(parse_transform(attrs.get("transform", "")), root_x)
        subs = flatten(attrs["d"], m)
        if subs:
            out.append((subs, fill))
    if not out:
        raise ValueError(f"{path}: no filled <path> elements")
    return out


def bbox(shapes):
    xs = [p[0] for subs, _ in shapes for sub in subs for p in sub]
    ys = [p[1] for subs, _ in shapes for sub in subs for p in sub]
    return min(xs), min(ys), max(xs), max(ys)


def coverage(shapes, w, h, ss=8, pad=0.0):
    """Letterbox `shapes` into a w*h box and return per-pixel coverage, 0-255.

    The fit is aspect-preserving and centred in continuous coordinates, not by
    integer division of a supersampled slot — which is the off-by-half-a-
    supersample bug gen_icons.py's mask_fit() had to grow an `even_w` option to
    work around. Here the whole transform is float, so a shape that is
    symmetric in the source stays symmetric in the mask.

    Even-odd, by XOR of one filled layer per subpath. Each source path element
    is XORed within itself and then unioned (max) with the others, so a file
    whose art is two overlapping paths — the Terminid's two claws — does not
    punch a hole where they meet.
    """
    x0, y0, x1, y1 = bbox(shapes)
    sw, sh = max(x1 - x0, 1e-9), max(y1 - y0, 1e-9)
    box_w, box_h = w - 2 * pad, h - 2 * pad
    s = min(box_w / sw, box_h / sh)
    ox = pad + (box_w - sw * s) / 2.0 - x0 * s
    oy = pad + (box_h - sh * s) / 2.0 - y0 * s

    acc = Image.new("L", (w, h), 0)
    for subs, _ in shapes:
        layer = Image.new("1", (w * ss, h * ss), 0)
        for sub in subs:
            if len(sub) < 3:
                continue
            one = Image.new("1", (w * ss, h * ss), 0)
            ImageDraw.Draw(one).polygon(
                [((x * s + ox) * ss, (y * s + oy) * ss) for x, y in sub], fill=1)
            layer = ImageChops.logical_xor(layer, one)
        acc = ImageChops.lighter(acc, layer.convert("L").resize((w, h), Image.BOX))
    return acc


def solid_fill(shapes):
    """The one colour these shapes are filled with, as (r, g, b).

    Every icon in tools/assets/official_icons/ is a single flat hue — that hue
    *is* the faction's identity — so a file that turns out to carry two is a
    change this pipeline should be told about rather than average away.
    """
    fills = {f.lower() for _, f in shapes}
    if len(fills) != 1:
        raise ValueError(f"expected one fill colour, got {sorted(fills)}")
    c = fills.pop().lstrip("#")
    if len(c) == 3:
        c = "".join(ch * 2 for ch in c)
    if len(c) != 6:
        raise ValueError(f"fill {c!r} is not a #rrggbb hex colour")
    return tuple(int(c[i:i + 2], 16) for i in (0, 2, 4))

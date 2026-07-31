from PIL import Image, ImageDraw, ImageFont, ImageFilter
import numpy as np

BOLD = "/tmp/Anton-Regular.ttf"
REG = "/usr/share/fonts/noto/NotoSans-Regular.ttf"
REGBOLD = "/usr/share/fonts/noto/NotoSans-Bold.ttf"

BG = (8, 10, 14)
BG2 = (14, 17, 23)
GOLD = (223, 178, 79)
GOLD_DIM = (140, 111, 50)
RED = (198, 58, 47)
BLUE = (74, 140, 199)
WHITE = (232, 234, 237)
GREY = (120, 128, 138)
GREEN = (92, 168, 96)
NAVY = (22, 30, 46)
NAVY_DARK = (14, 19, 30)

SCALE = 3
W, H = 480*SCALE, 320*SCALE

def font(path, size):
    return ImageFont.truetype(path, size)

def load_silhouette(path, color, target_h):
    im = Image.open(path).convert("RGBA")
    arr = np.array(im)
    alpha = arr[:,:,3].astype(float)
    whiteness = (arr[:,:,0].astype(int) + arr[:,:,1].astype(int) + arr[:,:,2].astype(int))
    is_bg = (alpha < 10) | (whiteness > 720)
    mask = np.where(is_bg, 0, 255).astype('uint8')
    mask_img = Image.fromarray(mask, mode="L")
    bbox = mask_img.getbbox()
    if bbox:
        mask_img = mask_img.crop(bbox)
    w, h = mask_img.size
    new_w = max(1, int(w * target_h / h))
    mask_img = mask_img.resize((new_w, target_h), Image.LANCZOS)
    solid = Image.new("RGBA", mask_img.size, color + (0,))
    solid.putalpha(mask_img)
    return solid

def load_traced_skull(target_h, color=(20,22,28)):
    """Load the pixel-traced skull mask (extracted directly from real reference image) and clean it up."""
    mask_img = Image.open("/var/minis/attachments/skull_traced_mask.png").convert("RGBA")
    alpha = np.array(mask_img)[:,:,3]
    mask = Image.fromarray(alpha, mode="L")
    # upscale big then apply slight blur + rethreshold to smooth jpeg-jaggies without changing the shape
    big = mask.resize((mask.width*12, mask.height*12), Image.LANCZOS)
    big = big.filter(ImageFilter.GaussianBlur(9))
    big_arr = np.array(big)
    big_arr = np.where(big_arr > 110, 255, 0).astype('uint8')
    smooth_mask = Image.fromarray(big_arr, mode="L")
    smooth_mask = Image.fromarray(np.array(Image.fromarray(big_arr).filter(ImageFilter.GaussianBlur(4))))
    bbox = smooth_mask.getbbox()
    if bbox:
        smooth_mask = smooth_mask.crop(bbox)
    w, h = smooth_mask.size
    new_w = max(1, int(w * target_h / h))
    smooth_mask = smooth_mask.resize((new_w, target_h), Image.LANCZOS)
    solid = Image.new("RGBA", smooth_mask.size, color + (0,))
    solid.putalpha(smooth_mask)
    return solid

def draw_skull_badge(size, ring_color=WHITE, skull_color=(20,22,28), arc_color=WHITE):
    """Draw the Major Order skull badge: white circle + traced skull + triple arc sweep below."""
    S = size * 4  # supersample
    img = Image.new("RGBA", (S, S), (0,0,0,0))
    d = ImageDraw.Draw(img)
    cx, cy = S//2, int(S*0.42)
    r = int(S*0.36)

    # triple concentric arcs below the circle (drawn first, behind)
    for i, rr in enumerate([r*1.18, r*1.30, r*1.42]):
        bbox = [cx-rr, cy-rr, cx+rr, cy+rr]
        d.arc(bbox, start=35, end=145, fill=arc_color, width=max(2, S//60))

    # white filled circle
    d.ellipse([cx-r, cy-r, cx+r, cy+r], fill=ring_color)

    # traced skull (real shape extracted from reference), centered in circle
    skull_h = int(r * 1.55)
    skull = load_traced_skull(skull_h, color=skull_color)
    sx = cx - skull.width // 2
    sy = cy - skull.height // 2 - int(r*0.04)
    img.paste(skull, (sx, sy), skull)

    img = img.resize((size, size), Image.LANCZOS)
    return img

def draw_earth_bg_strip(w, h, earth_path):
    """Blurred, dimmed Earth image cropped to a wide strip for the header background."""
    earth = Image.open(earth_path).convert("RGB")
    # scale so the globe is much larger than the strip height (we only see a curved edge)
    target_h = int(h * 5)
    scale = target_h / earth.height
    earth_big = earth.resize((int(earth.width*scale), target_h), Image.LANCZOS)
    # take a slice from the right side of the globe, vertically centered on the globe's middle
    x0 = int(earth_big.width * 0.55)
    y0 = (earth_big.height - h) // 2
    x0 = max(0, min(x0, earth_big.width - w))
    strip = earth_big.crop((x0, y0, x0+w, y0+h))
    strip = strip.filter(ImageFilter.GaussianBlur(2))
    # brighten slightly and tint navy-blue so it reads as background art, not full photo
    arr = np.array(strip).astype(float)
    tint = np.array([40,55,85])
    arr = arr * 0.55 + tint * 0.45
    arr = np.clip(arr, 0, 255).astype('uint8')
    return Image.fromarray(arr)

def draw_header_bar(w, h, title="MAJOR ORDER", countdown="18H 22M"):
    img = Image.new("RGB", (w, h), NAVY_DARK)
    d = ImageDraw.Draw(img)

    # background earth art on the right side, faded into navy on the left via gradient mask
    earth_strip = draw_earth_bg_strip(w, h, "/var/minis/attachments/earth_nasa.jpg")
    mask = Image.new("L", (w, h), 0)
    md = ImageDraw.Draw(mask)
    for x in range(w):
        # fade in from ~22% width to full opacity by ~55% width
        t = (x - w*0.22) / (w*0.33)
        t = max(0.0, min(1.0, t))
        alpha = int(255 * t)
        md.line([(x,0),(x,h)], fill=alpha)
    img.paste(earth_strip, (0,0), mask)

    # diagonal gold light-shaft sweep
    sweep = Image.new("RGBA", (w, h), (0,0,0,0))
    sd = ImageDraw.Draw(sweep)
    sweep_x = int(w*0.62)
    sweep_w = int(w*0.05)
    sd.polygon([(sweep_x, 0), (sweep_x+sweep_w, 0), (sweep_x-int(h*0.5)+sweep_w, h), (sweep_x-int(h*0.5), h)], fill=GOLD+(90,))
    img = Image.alpha_composite(img.convert("RGBA"), sweep).convert("RGB")
    d = ImageDraw.Draw(img)

    # top/bottom thin border accents
    d.rectangle([0,0,w-1,h-1], outline=(50,54,44), width=1)

    # skull badge, left side
    badge_size = int(h*0.82)
    badge = draw_skull_badge(badge_size)
    badge_x = int(h*0.12)
    badge_y = (h - badge_size)//2
    img.paste(badge, (badge_x, badge_y), badge)

    # title text
    f_title = font(BOLD, int(h*0.42))
    title_x = badge_x + badge_size + int(h*0.18)
    bbox = d.textbbox((0,0), title, font=f_title)
    text_h = bbox[3]-bbox[1]
    d.text((title_x, (h-text_h)//2 - bbox[1]), title, font=f_title, fill=WHITE)

    return img

# Build header at device scale then upscale for preview
h_bar = draw_header_bar(480*SCALE, int(58*SCALE))
h_bar.save("/var/minis/attachments/header_bar_v5.png")

print("done", h_bar.size)

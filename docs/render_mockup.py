#!/usr/bin/env python3
"""
Render a pixel-accurate mockup of the CYD oscilloscope UI.

It mirrors the geometry and colours used by ESP32_CYD_Oscilloscope.ino so the
image is a faithful preview of what the 320x240 TFT shows. Two panels are drawn:
  * left  -> trigger source = CH1  (internal, yellow level arrow)
  * right -> trigger source = EXT  (analog external input, red "EXT TRIG" tag)
"""

from PIL import Image, ImageDraw, ImageFont

SCALE = 3                      # 3x for a crisp, readable image
W, H = 320, 240                # logical screen size (matches firmware)

# ---- geometry (identical to the firmware) --------------------------------
PLOT_X, PLOT_Y = 40, 26
PLOT_W, PLOT_H = 276, 172
PLOT_X1, PLOT_Y1 = PLOT_X + PLOT_W, PLOT_Y + PLOT_H
DIVS_H, DIVS_V = 8, 8
SAMPLES = PLOT_W

# ---- colours (RGB565 -> RGB888) ------------------------------------------
def rgb565(c):
    r = (c >> 11) & 0x1F
    g = (c >> 5) & 0x3F
    b = c & 0x1F
    return (r * 255 // 31, g * 255 // 63, b * 255 // 31)

COL_BG       = rgb565(0x0000)
COL_GRID     = rgb565(0x4208)
COL_GRID_CTR = rgb565(0x7BEF)
COL_TRACE    = rgb565(0x07E0)
COL_TEXT     = rgb565(0xFFFF)
COL_ACCENT   = rgb565(0xFFE0)
COL_EXT      = rgb565(0xF800)
COL_BAR      = rgb565(0x18E3)
COL_BTN      = rgb565(0x2C7B)
COL_BTN_ACT  = rgb565(0x05BF)

# ---- fonts (TFT_eSPI font1 ~8px, font2 ~16px tall) -----------------------
FONT_DIR = "/usr/share/fonts/truetype/dejavu/"
F1 = ImageFont.truetype(FONT_DIR + "DejaVuSansMono.ttf", int(8 * SCALE))
F1B = ImageFont.truetype(FONT_DIR + "DejaVuSansMono-Bold.ttf", int(8 * SCALE))
F2 = ImageFont.truetype(FONT_DIR + "DejaVuSansMono-Bold.ttf", int(16 * SCALE))
CAP = ImageFont.truetype(FONT_DIR + "DejaVuSans-Bold.ttf", int(11 * SCALE))

def S(v):
    return int(round(v * SCALE))


class Panel:
    """Draws one 320x240 screen at SCALE onto an ImageDraw canvas."""
    def __init__(self, img, ox, oy):
        self.d = ImageDraw.Draw(img)
        self.ox, self.oy = ox, oy

    def rect(self, x, y, w, h, col):
        self.d.rectangle([self.ox + S(x), self.oy + S(y),
                          self.ox + S(x + w) - 1, self.oy + S(y + h) - 1], fill=col)

    def line(self, x0, y0, x1, y1, col, wpx=1):
        self.d.line([self.ox + S(x0), self.oy + S(y0),
                     self.ox + S(x1), self.oy + S(y1)], fill=col, width=max(1, S(wpx)))

    def text(self, s, x, y, font, col, anchor="la"):
        self.d.text((self.ox + S(x), self.oy + S(y)), s, font=font, fill=col, anchor=anchor)

    def round_rect(self, x, y, w, h, r, col):
        self.d.rounded_rectangle([self.ox + S(x), self.oy + S(y),
                                  self.ox + S(x + w) - 1, self.oy + S(y + h) - 1],
                                 radius=S(r), fill=col)


def draw_screen(p, ext_mode, mode="AUTO", holdoff=0,
                vpp=3.30, freq=1000.0, vmin=0.00, vmax=3.30, vavg=1.65):
    """Draw the full UI. ext_mode selects CH1 vs EXT trigger source."""
    p.rect(0, 0, W, H, COL_BG)

    # ---- title ----
    p.text("CYD OSCILLOSCOPE", 6, 4, F2, COL_ACCENT)
    p.text("CH1:GPIO35", 232, 6, F1, COL_TEXT)

    # ---- status line (edge source mode  timebase  V/div  level  holdoff) ----
    src = "EXT" if ext_mode else "CH1"
    p.text(f"RISE {src} {mode}  1 ms/div 0.5 V/div L1.65V HO{holdoff}ms",
           6, 16, F1, COL_TEXT)

    # ---- grid ----
    dx = PLOT_W // DIVS_H
    dy = PLOT_H // DIVS_V
    for i in range(DIVS_H + 1):
        x = PLOT_X + i * dx
        c = COL_GRID_CTR if i == DIVS_H // 2 else COL_GRID
        p.line(x, PLOT_Y, x, PLOT_Y + PLOT_H, c)
    for i in range(DIVS_V + 1):
        y = PLOT_Y + i * dy
        c = COL_GRID_CTR if i == DIVS_V // 2 else COL_GRID
        p.line(PLOT_X, y, PLOT_X + PLOT_W, y, c)

    # ---- left axis ticks ----
    for i in range(DIVS_V + 1):
        y = PLOT_Y + i * dy
        v = 1.65 + (DIVS_V // 2 - i) * 0.5
        p.text(f"{v:.1f}", PLOT_X - 4, y, F1, COL_TEXT, anchor="rm")

    # ---- trace: a clean square wave, one rising edge ~1/8 in from left ----
    def volt_to_y(v):
        ppd = PLOT_H / DIVS_V
        y = (PLOT_Y + PLOT_H / 2.0) - ((v - 1.65) / 0.5) * ppd
        return max(PLOT_Y, min(PLOT_Y1, y))

    lo, hi = 0.15, 3.15
    period = SAMPLES / 5.0            # 5 cycles across the screen
    pts = []
    for i in range(SAMPLES):
        ph = ((i - SAMPLES / 8.0) % period) / period
        v = hi if ph < 0.5 else lo
        pts.append((PLOT_X + i, volt_to_y(v)))
    for i in range(1, len(pts)):
        p.line(pts[i - 1][0], pts[i - 1][1], pts[i][0], pts[i][1], COL_TRACE, 2)

    # ---- trigger marker ----
    if not ext_mode:
        y = volt_to_y(1.65)
        p.d.polygon([(p.ox + S(PLOT_X - 8), p.oy + S(y)),
                     (p.ox + S(PLOT_X - 2), p.oy + S(y - 4)),
                     (p.ox + S(PLOT_X - 2), p.oy + S(y + 4))], fill=COL_ACCENT)
    else:
        p.text("EXT TRIG 1.65V", PLOT_X + 2, PLOT_Y + 1, F1, COL_EXT)

    # ---- measurements ----
    trig_col = COL_EXT if ext_mode else COL_ACCENT
    p.text(f"Trig 1.65V {src}", PLOT_X + 2, PLOT_Y + 14, F1, trig_col)
    p.text(f"Vpp {vpp:.2f}V  F {freq:.1f}Hz", PLOT_X1 - 2, PLOT_Y + 2, F1, COL_ACCENT, anchor="ra")
    p.text(f"min {vmin:.2f}  max {vmax:.2f}  avg {vavg:.2f}",
           PLOT_X1 - 2, PLOT_Y1 - 2, F1, COL_TEXT, anchor="rd")

    # ---- control bar: 12 buttons in two rows of six ----
    p.rect(0, 200, W, 40, COL_BAR)
    row1 = ["T-", "T+", "V-", "V+", "L-", "L+"]
    row2 = ["RISE", src, "AUTO", "MODE", "HOLD", "RUN"]
    xs = [2, 55, 108, 161, 214, 267]
    for lab, x in zip(row1, xs):
        p.round_rect(x + 1, 202 + 1, 52 - 2, 18 - 2, 4, COL_BTN)
        p.text(lab, x + 26, 202 + 9, F1B, COL_TEXT, anchor="mm")
    for lab, x in zip(row2, xs):
        p.round_rect(x + 1, 222 + 1, 52 - 2, 18 - 2, 4, COL_BTN)
        p.text(lab, x + 26, 222 + 9, F1B, COL_TEXT, anchor="mm")


def main():
    gap = S(24)
    cap_h = S(26)
    total_w = S(W) * 2 + gap
    total_h = cap_h + S(H)
    img = Image.new("RGB", (total_w, total_h), (18, 18, 18))
    d = ImageDraw.Draw(img)

    # captions
    d.text((S(W) // 2, S(6)), "CH1 trigger  (internal)  •  mode AUTO",
           font=CAP, fill=(235, 235, 235), anchor="ma")
    d.text((S(W) + gap + S(W) // 2, S(6)), "EXT trigger  (analog)  •  mode NORMAL",
           font=CAP, fill=(255, 120, 120), anchor="ma")

    # panel 1: CH1, AUTO mode, no holdoff
    p1 = Panel(img, 0, cap_h)
    draw_screen(p1, ext_mode=False, mode="AUTO", holdoff=0)

    # panel 2: EXT, NORMAL mode, 10 ms holdoff
    p2 = Panel(img, S(W) + gap, cap_h)
    draw_screen(p2, ext_mode=True, mode="NORM", holdoff=10)

    # thin bezel around each screen
    for ox in (0, S(W) + gap):
        d.rectangle([ox - 1, cap_h - 1, ox + S(W), cap_h + S(H)], outline=(90, 90, 90))

    img.save("docs/ui_mockup.png")
    print("wrote docs/ui_mockup.png", img.size)


if __name__ == "__main__":
    main()

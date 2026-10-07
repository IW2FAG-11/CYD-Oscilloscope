#!/usr/bin/env python3
"""
Render an accurate input-wiring diagram for the CYD oscilloscope.

Both inputs (CH1 on GPIO35, analog EXT trigger on GPIO27) use the same
100k/10k divider + 3.3 V Zener clamp front-end. Drawn with PIL so all text
is crisp and consistent with the README.
"""

from PIL import Image, ImageDraw, ImageFont

W, H = 1500, 940
BG = (255, 255, 255)
INK = (25, 25, 30)
BLUE = (20, 90, 200)
BLUE_L = (232, 240, 253)
YEL = (150, 120, 0)
YEL_L = (255, 250, 230)
RED = (200, 30, 30)
GREY = (120, 120, 130)
GREEN = (20, 140, 70)

FD = "/usr/share/fonts/truetype/dejavu/"
def f(name, size):
    return ImageFont.truetype(FD + name, size)
F_TITLE = f("DejaVuSans-Bold.ttf", 40)
F_SUB   = f("DejaVuSans.ttf", 22)
F_BLK   = f("DejaVuSans-Bold.ttf", 26)
F_LBL   = f("DejaVuSans.ttf", 20)
F_LBLB  = f("DejaVuSans-Bold.ttf", 20)
F_SM    = f("DejaVuSans.ttf", 18)
F_SMB   = f("DejaVuSans-Bold.ttf", 18)

img = Image.new("RGB", (W, H), BG)
d = ImageDraw.Draw(img)

def wire(x0, y0, x1, y1, col=INK, w=3):
    d.line([x0, y0, x1, y1], fill=col, width=w)

def dot(x, y, col=INK, r=6):
    d.ellipse([x - r, y - r, x + r, y + r], fill=col)

def terminal(x, y, col=INK, r=8):
    d.ellipse([x - r, y - r, x + r, y + r], outline=col, width=3, fill=(255, 255, 255))

def resistor(cx, cy, horizontal=True, col=INK):
    """IEC rectangle resistor centred at (cx,cy)."""
    if horizontal:
        w, h = 110, 34
        d.rectangle([cx - w // 2, cy - h // 2, cx + w // 2, cy + h // 2],
                    outline=col, width=3, fill=(255, 255, 255))
    else:
        w, h = 34, 110
        d.rectangle([cx - w // 2, cy - h // 2, cx + w // 2, cy + h // 2],
                    outline=col, width=3, fill=(255, 255, 255))

def gnd(x, y, col=INK):
    """Ground symbol with its top at (x,y)."""
    wire(x, y, x, y + 14, col)
    for i, hw in enumerate((26, 17, 8)):
        yy = y + 14 + i * 8
        d.line([x - hw, yy, x + hw, yy], fill=col, width=4)

def zener(x, ytop, ybot, col=INK):
    """Vertical Zener diode between ytop and ybot at x (cathode at top)."""
    mid = (ytop + ybot) // 2
    tri = 26
    # anode triangle pointing up to the bar
    d.polygon([(x - tri, mid + tri), (x + tri, mid + tri), (x, mid - 6)], outline=col)
    d.line([(x - tri, mid + tri), (x + tri, mid + tri), (x, mid - 6), (x - tri, mid + tri)],
           fill=col, width=3)
    # cathode bar with Zener "wings"
    d.line([x - tri, mid - 6, x + tri, mid - 6], fill=col, width=4)
    d.line([x - tri, mid - 6, x - tri - 8, mid - 14], fill=col, width=4)
    d.line([x + tri, mid - 6, x + tri + 8, mid + 2], fill=col, width=4)
    wire(x, ytop, x, mid - 6, col)
    wire(x, mid + tri, x, ybot, col)

# ---------------- title ----------------
d.text((40, 26), "CYD Oscilloscope — Input Wiring", font=F_TITLE, fill=INK)
d.text((40, 78),
       "ESP32-2432S028R  •  CH1 trace on GPIO35 (P3)  •  analog EXT trigger on GPIO27 (CN1)",
       font=F_SUB, fill=GREY)
d.line([40, 116, W - 40, 116], fill=(220, 220, 225), width=2)

# note banner
d.rounded_rectangle([40, 130, W - 40, 178], radius=10, fill=(245, 246, 250),
                    outline=(210, 214, 224), width=2)
d.text((58, 143),
       "Both inputs are 0–3.3 V AT THE PIN. The 100k/10k divider is ~11:1, so each input "
       "accepts up to ~36 V (multiply on-screen values by 11).",
       font=F_SM, fill=INK)

def draw_block(y0, title, title_col, title_fill, pin, adc, in_label, note):
    # panel
    d.rounded_rectangle([40, y0, W - 40, y0 + 300], radius=14, fill=(255, 255, 255),
                        outline=title_col, width=3)
    d.rounded_rectangle([40, y0, 430, y0 + 46], radius=14, fill=title_fill)
    d.rectangle([40, y0 + 24, 430, y0 + 46], fill=title_fill)
    d.text((60, y0 + 10), title, font=F_BLK, fill=title_col)

    y = y0 + 150                       # horizontal signal line
    x_in = 90
    x_r1 = 250                         # 100k
    x_nodeA = 470                      # 10k branch
    x_nodeB = 700                      # zener branch
    x_pin = 1080

    # input terminal
    terminal(x_in, y, title_col)
    d.text((x_in - 8, y - 60), in_label, font=F_LBLB, fill=INK)
    wire(x_in + 8, y, x_r1 - 55, y)

    # 100k series
    resistor(x_r1, y, horizontal=True)
    d.text((x_r1, y - 44), "100k", font=F_LBLB, fill=INK, anchor="ma")
    wire(x_r1 + 55, y, x_nodeA, y)

    # node A -> 10k -> GND
    dot(x_nodeA, y)
    wire(x_nodeA, y, x_nodeA, y + 60)
    resistor(x_nodeA, y + 115, horizontal=False)
    d.text((x_nodeA + 34, y + 115), "10k", font=F_LBLB, fill=INK, anchor="lm")
    wire(x_nodeA, y + 170, x_nodeA, y + 200)
    gnd(x_nodeA, y + 200)
    d.text((x_nodeA, y + 258), "GND", font=F_SMB, fill=GREY, anchor="ma")

    # continue to node B -> Zener -> GND
    wire(x_nodeA, y, x_nodeB, y)
    dot(x_nodeB, y)
    zener(x_nodeB, y + 40, y + 200)
    d.text((x_nodeB + 40, y + 120), "3.3 V", font=F_SMB, fill=INK, anchor="lm")
    d.text((x_nodeB + 40, y + 142), "Zener", font=F_SMB, fill=INK, anchor="lm")
    gnd(x_nodeB, y + 200)

    # continue to GPIO pin
    wire(x_nodeB, y, x_pin - 8, y)
    terminal(x_pin, y, title_col)
    d.text((x_pin + 22, y - 40), pin, font=F_BLK, fill=title_col)
    d.text((x_pin + 22, y - 8), adc, font=F_SM, fill=GREY)
    d.text((x_pin + 22, y + 20), note, font=F_SM, fill=GREY)

# ---- Block A: CH1 ----
draw_block(200, "CH1  —  displayed trace", BLUE, BLUE_L,
           "GPIO35", "ADC1_CH7  •  input-only  •  header P3",
           "CH1 probe", "works with WiFi on")

# ---- Block B: EXT trigger ----
draw_block(560, "EXT trigger  —  analog input", YEL, YEL_L,
           "GPIO27", "ADC2_CH7  •  header CN1",
           "EXT trigger", "WiFi off; may have 10k pull-up")

# ---- bottom warnings ----
d.rounded_rectangle([40, 884, W - 40, 928], radius=10, fill=(255, 235, 235),
                    outline=RED, width=2)
d.text((60, 895),
       "⚠  Never exceed 3.3 V at a GPIO pin — higher voltage will damage the ESP32. "
       "Keep leads short and add the Zener clamp.",
       font=F_SMB, fill=RED)

img.save("docs/wiring.png")
print("wrote docs/wiring.png", img.size)

#!/usr/bin/env python3
from PIL import Image, ImageDraw, ImageFont
import os

# Config
WIDTH, HEIGHT = 48, 68  # target canvas
POINTSIZE = 88          # 84 tuned earlier for LECO
FONT_FAMILY = 'leco'
FONT_SPECIFICATION = '-regular'
FONT_SPECIFICATION_SHORT = FONT_SPECIFICATION.split('-')[-1]
FONT_PATH = os.path.join(os.path.dirname(__file__), '..', 'resources', 'fonts', f'{FONT_FAMILY}{FONT_SPECIFICATION}.ttf')
OUT_DIR = os.path.join(os.path.dirname(__file__), '..', 'resources', 'images')

# Colors
BLACK = (0, 0, 0, 255)
WHITE = (255, 255, 255, 255)
TRANSPARENT = (0, 0, 0, 0)

# Load font
font = ImageFont.truetype(FONT_PATH, POINTSIZE)

# Vertical tweak to visually center in 68px canvas
# This may need a small offset depending on baseline; start at +1
Y_OFFSET = 0

for digit in range(10):
    d = str(digit)
    # Measure text size
    # getbbox works better for vertical metrics in Pillow >=8.0
    bbox = font.getbbox(d)
    text_w = bbox[2] - bbox[0]
    text_h = bbox[3] - bbox[1]

    # Center positions
    x = (WIDTH - text_w) // 2 - bbox[0]
    y = (HEIGHT - text_h) // 2 - bbox[1] + Y_OFFSET

    for variant, fill, name in [
        ('black', BLACK, f'digit_{digit}_{FONT_FAMILY}{"_" + FONT_SPECIFICATION_SHORT if FONT_SPECIFICATION_SHORT else ""}_{POINTSIZE}_black.png'),
        ('white', WHITE, f'digit_{digit}_{FONT_FAMILY}{"_" + FONT_SPECIFICATION_SHORT if FONT_SPECIFICATION_SHORT else ""}_{POINTSIZE}_white.png'),
    ]:
        # Set background opposite to digit color
        bg = WHITE if fill == BLACK else BLACK
        img = Image.new('RGBA', (WIDTH, HEIGHT), bg)
        draw = ImageDraw.Draw(img)
        # Disable anti-aliasing by using bitmap mode
        draw.fontmode = "1"  # 1-bit rendering, no anti-aliasing
        draw.text((x, y), d, font=font, fill=fill)
        out_path = os.path.join(OUT_DIR, name)
        img.save(out_path, optimize=True, compress_level=9)
        print('wrote', out_path)

# Also generate half-sized digits
if True:
    HALF_WIDTH, HALF_HEIGHT = WIDTH // 2, HEIGHT // 2
    HALF_POINTSIZE = (POINTSIZE - 4) // 2
    half_font = ImageFont.truetype(FONT_PATH, HALF_POINTSIZE)
    for digit in range(10):
        d = str(digit)
        # Measure text size
        bbox = half_font.getbbox(d)
        text_w = bbox[2] - bbox[0]
        text_h = bbox[3] - bbox[1]

        # Center positions
        x = (HALF_WIDTH - text_w) // 2 - bbox[0]
        y = (HALF_HEIGHT - text_h) // 2 - bbox[1]

        for variant, fill, name in [
            ('black', BLACK, f'digit_{digit}_{FONT_FAMILY}{"_" + FONT_SPECIFICATION_SHORT if FONT_SPECIFICATION_SHORT else ""}_{HALF_POINTSIZE}_black.png'),
            ('white', WHITE, f'digit_{digit}_{FONT_FAMILY}{"_" + FONT_SPECIFICATION_SHORT if FONT_SPECIFICATION_SHORT else ""}_{HALF_POINTSIZE}_white.png'),
        ]:
            # Set background opposite to digit color
            bg = WHITE if fill == BLACK else BLACK
            img = Image.new('RGBA', (HALF_WIDTH, HALF_HEIGHT), bg)
            draw = ImageDraw.Draw(img)
            # Disable anti-aliasing by using bitmap mode
            draw.fontmode = "1"  # 1-bit rendering, no anti-aliasing
            draw.text((x, y), d, font=half_font, fill=fill)
            out_path = os.path.join(OUT_DIR, name)
            img.save(out_path, optimize=True, compress_level=9)
            print('wrote', out_path)

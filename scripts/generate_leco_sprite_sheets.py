#!/usr/bin/env python3
from PIL import Image, ImageDraw, ImageFont
import os

# Config
DIGIT_WIDTH = 48   # Width per digit in sprite sheet
DIGIT_HEIGHT = 68  # Height of sprite sheet  
TOTAL_WIDTH = DIGIT_WIDTH * 10  # 10 digits: 0-9
POINTSIZE = 88     # Font size
FONT_FAMILY = 'leco'
FONT_SPECIFICATION = '-regular'
FONT_PATH = os.path.join(os.path.dirname(__file__), '..', 'resources', 'fonts', f'{FONT_FAMILY}{FONT_SPECIFICATION}.ttf')
OUT_DIR = os.path.join(os.path.dirname(__file__), '..', 'resources', 'images')

# Colors
BLACK = (0, 0, 0, 255)
WHITE = (255, 255, 255, 255)

# Load font
font = ImageFont.truetype(FONT_PATH, POINTSIZE)

def generate_sprite_sheet(pointsize, digit_width, digit_height, suffix=""):
    total_width = digit_width * 10
    font_obj = ImageFont.truetype(FONT_PATH, pointsize)
    
    for color_name, digit_color, bg_color in [
        ('black', BLACK, WHITE),
        ('white', WHITE, BLACK)
    ]:
        # Create sprite sheet image
        img = Image.new('RGBA', (total_width, digit_height), bg_color)
        draw = ImageDraw.Draw(img)
        draw.fontmode = "1"  # 1-bit rendering, no anti-aliasing
        
        # Draw each digit
        for digit in range(10):
            d = str(digit)
            
            # Calculate position for this digit
            x_offset = digit * digit_width
            
            # Measure text size for centering
            bbox = font_obj.getbbox(d)
            text_w = bbox[2] - bbox[0]
            text_h = bbox[3] - bbox[1]
            
            # Center within this digit's rectangle
            x = x_offset + (digit_width - text_w) // 2 - bbox[0]
            y = (digit_height - text_h) // 2 - bbox[1]
            
            # Draw the digit
            draw.text((x, y), d, font=font_obj, fill=digit_color)
        
        # Save sprite sheet
        filename = f'leco_regular_{pointsize}{suffix}_{color_name}.png'
        out_path = os.path.join(OUT_DIR, filename)
        img.save(out_path, optimize=True, compress_level=9)
        print(f'wrote {out_path}')

# Generate full-size sprite sheet (88pt)
generate_sprite_sheet(POINTSIZE, DIGIT_WIDTH, DIGIT_HEIGHT)

# Generate half-size sprite sheet (42pt)
HALF_POINTSIZE = (POINTSIZE - 4) // 2  # 42pt
HALF_WIDTH = DIGIT_WIDTH // 2          # 24px per digit
HALF_HEIGHT = DIGIT_HEIGHT // 2        # 34px high
generate_sprite_sheet(HALF_POINTSIZE, HALF_WIDTH, HALF_HEIGHT, "_small")

print("Generated LECO sprite sheets!")
print(f"Full size: {TOTAL_WIDTH}x{DIGIT_HEIGHT} ({DIGIT_WIDTH}px per digit)")
print(f"Small size: {HALF_WIDTH * 10}x{HALF_HEIGHT} ({HALF_WIDTH}px per digit)")
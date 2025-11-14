#!/usr/bin/env python3
from PIL import Image
import os

# Paths
INPUT_FILE = os.path.join(os.path.dirname(__file__), '..', 'resources', 'images', 'Numbers_24w_30h.png')
OUT_DIR = os.path.join(os.path.dirname(__file__), '..', 'resources', 'images')

# Load the original sprite sheet
img = Image.open(INPUT_FILE)
original_width, original_height = img.size

print(f"Original size: {original_width}x{original_height}")

# Create different sizes
sizes = [
    (0.5, "small"),   # Half size: 12x15 per digit, 120x15 total
    (1.0, "medium"),  # Original size: 24x30 per digit, 240x30 total  
    (2.0, "large"),   # Double size: 48x60 per digit, 480x60 total
]

for scale, name in sizes:
    new_width = int(original_width * scale)
    new_height = int(original_height * scale)
    
    if scale == 1.0:
        # Just copy the original
        resized_img = img.copy()
    else:
        # Resize using Lanczos for smooth curves and high quality
        resized_img = img.resize((new_width, new_height), Image.LANCZOS)
    
    # Save with descriptive names showing actual dimensions
    out_path = os.path.join(OUT_DIR, f'stolen_numbers_{name}_{new_width}x{new_height}.png')
    resized_img.save(out_path, optimize=True)
    
    element_width = new_width / 11  # 11 elements total (0-9 + empty space)
    print(f"Created {name}: {new_width}x{new_height} ({element_width:.1f}px per element) -> {out_path}")

print("\nResized sprite sheets created with smooth scaling!")
print("Each contains 11 elements (digits 0-9 + empty space) arranged horizontally")
print("Use digit_index * element_spacing as x-offset to select specific digits")
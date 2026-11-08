# ==============================================================================
# Date:     11/07/2026
# Program:  Weather Keychain
# File:     image_to_array.py
#
# Purpose:  Convert a picture into a 128 x 64, one-bit C++ OLED bitmap array.
#           Adapted from github.com/bam-project/edge-aware-pixel-art.
# Requires: pip install pillow numpy opencv-python
# ==============================================================================

import re

import cv2
import numpy as np
from PIL import Image, ImageOps


# ------------------------------------------------------------------------------
# image_to_array
# Purpose:  Center-crop a photo, preserve dark edges, and pack OLED pixels.
# Args:     image_path - input image filename.
#           name - C++ array name.
#           block_size - pixel-art block width and height (1 to 10).
#           threshold - monochrome brightness cutoff (0 to 255).
#           dither - use dotted shading instead of a single brightness cutoff.
#           invert - exchange on and off pixels.
# Returns:  C++ declaration containing 1024 bytes, ready to paste into a header.
# ------------------------------------------------------------------------------
def image_to_array(
    image_path,
    name="kCustomArtwork",
    block_size=2,
    threshold=128,
    dither=True,
    invert=False,
):
    """Return a PROGMEM array compatible with Adafruit GFX drawBitmap()."""
    if not re.fullmatch(r"[A-Za-z][A-Za-z0-9_]*", name):
        raise ValueError("Use a C++ array name starting with a letter.")
    if isinstance(block_size, bool) or not isinstance(block_size, int):
        raise ValueError("block_size must be an integer from 1 to 10.")
    if not 1 <= block_size <= 10:
        raise ValueError("block_size must be an integer from 1 to 10.")
    if not 0 <= threshold <= 255:
        raise ValueError("threshold must be from 0 to 255.")

    width = 128
    height = 64

    # Correct phone-photo orientation and place transparent pixels over black.
    with Image.open(image_path) as original:
        picture = ImageOps.exif_transpose(original).convert("RGBA")
        background = Image.new("RGBA", picture.size, (0, 0, 0, 255))
        picture = Image.alpha_composite(background, picture).convert("RGB")
        picture = ImageOps.fit(
            picture,
            (width, height),
            method=Image.Resampling.LANCZOS,
        )
        colors = np.asarray(picture)

    # Match the original edge-aware algorithm's grayscale and Canny settings.
    grayscale = colors.mean(axis=2).astype(np.uint8)
    blurred = cv2.GaussianBlur(grayscale, (3, 3), 0)
    edges = cv2.Canny(blurred, 50, 150) > 0
    tones = np.zeros((height, width), dtype=np.float32)

    # Repeat edge pixels when the image does not divide evenly into blocks.
    bottom_padding = (-height) % block_size
    right_padding = (-width) % block_size
    padded_colors = np.pad(
        colors,
        ((0, bottom_padding), (0, right_padding), (0, 0)),
        mode="edge",
    )
    padded_edges = np.pad(
        edges,
        ((0, bottom_padding), (0, right_padding)),
        mode="edge",
    )

    for top in range(0, height, block_size):
        for left in range(0, width, block_size):
            block = padded_colors[top:top + block_size, left:left + block_size]
            block_edges = padded_edges[top:top + block_size, left:left + block_size]
            pixels = block.reshape(-1, 3)
            dark_pixels = pixels.mean(axis=1) < 60

            # Preserve dark features only when both source conditions are met.
            if dark_pixels.mean() >= 0.30 and block_edges.mean() >= 0.05:
                color = pixels[dark_pixels].mean(axis=0)
            else:
                color = np.median(pixels, axis=0)

            # The source algorithm truncates each channel before output.
            brightness = color.astype(np.uint8).mean()
            tones[top:top + block_size, left:left + block_size] = brightness

    # A blue OLED has only on/off pixels; Bayer dithering suggests intermediate tones.
    cutoff = np.full((height, width), threshold, dtype=np.float32)
    if dither:
        bayer = np.array([
            [0, 8, 2, 10],
            [12, 4, 14, 6],
            [3, 11, 1, 9],
            [15, 7, 13, 5],
        ], dtype=np.float32)
        cutoff += np.tile((bayer - 7.5) * 16, (height // 4, width // 4))

    on_pixels = tones > cutoff
    if invert:
        on_pixels = ~on_pixels

    # Each row uses 16 bytes. The leftmost pixel is the most significant bit.
    packed = np.packbits(on_pixels, axis=1, bitorder="big").flatten()
    lines = []
    for start in range(0, len(packed), 16):
        row = packed[start:start + 16]
        lines.append("  " + ", ".join(f"0x{value:02X}" for value in row) + ",")

    return (
        "// 128 x 64 pixels; row-major, MSB first; 1 = on, 0 = off.\n"
        f"const uint8_t {name}[1024] PROGMEM = {{\n"
        + "\n".join(lines)
        + "\n};\n"
    )

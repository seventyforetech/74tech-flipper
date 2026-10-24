#!/usr/bin/env python3
"""Generates the 1-bit PNG assets for the 74 Tech Flipper Zero app.

Run automatically by the GitHub Actions workflow before building.
Creates:
  images/mascot.png  (32x32, fluffy bot mascot used on-screen)
  icon.png           (10x10, app icon shown in the Flipper apps list)
"""
import math
import os

from PIL import Image, ImageDraw

BLACK = 0  # in mode "1": 0 = black = pixel ON on the Flipper screen
WHITE = 1


def make_mascot():
    img = Image.new("1", (32, 32), WHITE)
    d = ImageDraw.Draw(img)

    # Antenna
    d.line([(16, 4), (16, 7)], fill=BLACK)
    d.ellipse([14, 0, 18, 4], fill=BLACK)

    # Fluffy body: black blob made of a big ellipse plus scallop circles
    cx, cy, rx, ry = 15.5, 18.0, 12.0, 10.0
    d.ellipse([cx - rx, cy - ry, cx + rx, cy + ry], fill=BLACK)
    for i in range(16):
        a = 2 * math.pi * i / 16
        px = cx + rx * math.cos(a)
        py = cy + ry * math.sin(a)
        d.ellipse([px - 2.6, py - 2.6, px + 2.6, py + 2.6], fill=BLACK)

    # Inner white face area (leaves a fluffy outline)
    d.ellipse([cx - rx + 3, cy - ry + 3, cx + rx - 3, cy + ry - 3], fill=WHITE)

    # Eyes
    d.rectangle([9, 14, 12, 18], fill=BLACK)
    d.rectangle([19, 14, 22, 18], fill=BLACK)
    # Eye shine
    d.point([(10, 15), (20, 15)], fill=WHITE)

    # Smile
    d.line([(11, 20), (12, 22), (14, 23), (17, 23), (19, 22), (20, 20)], fill=BLACK)

    # Feet
    d.rectangle([9, 29, 13, 31], fill=BLACK)
    d.rectangle([18, 29, 22, 31], fill=BLACK)
    return img


def make_icon():
    img = Image.new("1", (10, 10), WHITE)
    d = ImageDraw.Draw(img)
    d.point([(4, 0), (5, 0)], fill=BLACK)  # antenna
    d.rectangle([1, 2, 8, 8], outline=BLACK, fill=WHITE)  # head
    d.point([(1, 2), (8, 2), (1, 8), (8, 8)], fill=WHITE)  # round corners
    d.rectangle([3, 4, 3, 5], fill=BLACK)  # eye
    d.rectangle([6, 4, 6, 5], fill=BLACK)  # eye
    d.line([(3, 7), (6, 7)], fill=BLACK)  # mouth
    d.point([(4, 1), (5, 1)], fill=BLACK)
    return img


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    os.makedirs(os.path.join(here, "images"), exist_ok=True)
    make_mascot().save(os.path.join(here, "images", "mascot.png"))
    make_icon().save(os.path.join(here, "icon.png"))
    print("Assets generated: images/mascot.png, icon.png")


if __name__ == "__main__":
    main()

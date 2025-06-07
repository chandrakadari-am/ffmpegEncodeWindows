import argparse
import os
import sys
from PIL import Image
import numpy as np

def bgra_to_png(bgra_file_path, width, height, png_file_path):
    # Read raw BGRA data
    with open(bgra_file_path, 'rb') as f:
        raw_data = f.read()

    # Convert raw data to numpy array (height, width, 4)
    img_data = np.frombuffer(raw_data, dtype=np.uint8)
    img_data = img_data.reshape((height, width, 4))

    # Convert BGRA to RGBA by swapping channels
    # BGRA order: B=0, G=1, R=2, A=3
    img_data = img_data[:, :, [2, 1, 0, 3]]

    # Create Pillow Image from RGBA data
    img = Image.fromarray(img_data, 'RGBA')

    # Save as PNG
    img.save(png_file_path)
    print(f"Saved PNG: {png_file_path}")

def main():
    parser = argparse.ArgumentParser(description="Convert raw BGRA image to PNG.")
    parser.add_argument("input", help="Path to input .bgra file")
    parser.add_argument("width", type=int, help="Width of the image")
    parser.add_argument("height", type=int, help="Height of the image")
    parser.add_argument("output", nargs='?', help="Output PNG file path (optional)")

    args = parser.parse_args()

    input_path = args.input
    width = args.width
    height = args.height
    output_path = args.output or os.path.splitext(input_path)[0] + ".png"

    if not os.path.isfile(input_path):
        print(f"Input file not found: {input_path}. Skipping conversion.")
        sys.exit(0)  # Or use return if part of bigger app

    bgra_to_png(input_path, width, height, output_path)

if __name__ == "__main__":
    main()

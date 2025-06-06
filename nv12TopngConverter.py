import numpy as np
import cv2
import argparse
import os

def nv12_to_png(nv12_path, width, height, output_path):
    # Read raw NV12 data
    with open(nv12_path, 'rb') as f:
        nv12_data = f.read()

    frame_size = width * height
    expected_size = frame_size * 3 // 2
    if len(nv12_data) < expected_size:
        raise ValueError(f"NV12 data is too small ({len(nv12_data)} bytes). Expected at least {expected_size} bytes.")

    # Convert raw data to numpy array
    yuv = np.frombuffer(nv12_data, dtype=np.uint8)
    yuv = yuv.reshape((height * 3 // 2, width))

    # Convert NV12 to BGR
    bgr_img = cv2.cvtColor(yuv, cv2.COLOR_YUV2BGR_NV12)

    # Save as PNG
    cv2.imwrite(output_path, bgr_img)
    print(f"Saved PNG to: {output_path}")

def main():
    parser = argparse.ArgumentParser(description="Convert NV12 raw image to PNG.")
    parser.add_argument("input", help="Path to input .nv12 file")
    parser.add_argument("width", type=int, help="Width of the image")
    parser.add_argument("height", type=int, help="Height of the image")
    parser.add_argument("output", nargs='?', help="Output PNG file path (optional)")

    args = parser.parse_args()

    input_path = args.input
    width = args.width
    height = args.height
    output_path = args.output or os.path.splitext(input_path)[0] + ".png"

    nv12_to_png(input_path, width, height, output_path)

if __name__ == "__main__":
    main()

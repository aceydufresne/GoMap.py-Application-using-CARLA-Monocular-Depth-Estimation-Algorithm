

import argparse
import colorsys
from pathlib import Path

import cv2
import numpy as np
from PIL import Image, ImageOps


def find_colored_contours(image, min_area=100, canny_low=40, canny_high=120,
                          outline_only=False, thickness=2, close_size=5,
                          contrast=1.0):

    gray = cv2.cvtColor(image, cv2.COLOR_BGR2GRAY)
    if contrast > 0:
        gray = cv2.createCLAHE(clipLimit=contrast, tileGridSize=(8, 8)).apply(gray)
    blurred = cv2.GaussianBlur(gray, (3, 3), 0)
    edges = cv2.Canny(blurred, canny_low, canny_high)
    if close_size > 1:

        kernel = cv2.getStructuringElement(cv2.MORPH_ELLIPSE,
                                          (close_size, close_size))
        edges = cv2.morphologyEx(edges, cv2.MORPH_CLOSE, kernel)

    contours, _ = cv2.findContours(edges, cv2.RETR_TREE,
                                   cv2.CHAIN_APPROX_SIMPLE)
    contours = [c for c in contours if cv2.contourArea(c) >= min_area]
    contours.sort(key=cv2.contourArea, reverse=True)

    canvas = np.full_like(image, (24, 24, 24))
    count = len(contours)

    step = max(1, count // 2)
    from math import gcd
    while count and gcd(step, count) != 1:
        step += 1
    for index, contour in enumerate(contours):
        hue = ((index * step) % count) / count
        rgb = colorsys.hsv_to_rgb(hue, 0.75, 1.0)
        color = tuple(round(channel * 255) for channel in rgb[::-1])

        cv2.drawContours(canvas, [contour], -1, color,
                         thickness if outline_only else cv2.FILLED,
                         lineType=cv2.LINE_8)
    return canvas, edges, contours


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('input', type=Path, help='Input image')
    parser.add_argument('--output', type=Path, help='Output PNG path')
    parser.add_argument('--min-area', type=float, default=100,
                        help='Minimum contour area in processed pixels (default: 100)')
    parser.add_argument('--max-side', type=int, default=1600,
                        help='Resize longest side for processing; 0 keeps full size')
    parser.add_argument('--canny-low', type=int, default=40)
    parser.add_argument('--canny-high', type=int, default=120)
    parser.add_argument('--close-size', type=int, default=5,
                        help='Odd gap-closing kernel size; 0 disables it (default: 5)')
    parser.add_argument('--contrast', type=float, default=1.0,
                        help='Local contrast enhancement; 0 disables it (default: 1)')
    parser.add_argument('--outline-only', action='store_true',
                        help='Draw colored boundaries instead of filled regions')
    parser.add_argument('--thickness', type=int, default=2)
    parser.add_argument('--no-display', action='store_true',
                        help='Save without opening an interactive window')
    args = parser.parse_args()
    if args.min_area < 0 or args.max_side < 0 or args.thickness < 1:
        parser.error('min-area and max-side must be nonnegative; thickness must be positive')
    if not 0 <= args.canny_low < args.canny_high <= 255:
        parser.error('Canny thresholds must satisfy 0 <= low < high <= 255')
    if args.close_size < 0 or (args.close_size and args.close_size % 2 == 0):
        parser.error('close-size must be 0 or a positive odd integer')
    if args.contrast < 0:
        parser.error('contrast must be nonnegative')

    with Image.open(args.input) as source:
        oriented = ImageOps.exif_transpose(source).convert('RGB')
        if args.max_side:
            oriented.thumbnail((args.max_side, args.max_side), Image.Resampling.LANCZOS)
        image = cv2.cvtColor(np.asarray(oriented), cv2.COLOR_RGB2BGR)

    colored, edges, contours = find_colored_contours(
        image, args.min_area, args.canny_low, args.canny_high,
        args.outline_only, args.thickness, args.close_size, args.contrast)
    output = args.output or args.input.with_name(args.input.stem + '_contours.png')
    if output.suffix.lower() != '.png':
        parser.error('--output must end in .png')
    if output.resolve() == args.input.resolve():
        parser.error('Output must be different from the input image')
    output.parent.mkdir(parents=True, exist_ok=True)

    Image.fromarray(cv2.cvtColor(colored, cv2.COLOR_BGR2RGB)).save(output)
    print(f'{len(contours)} contours; processed size {image.shape[1]} x {image.shape[0]}')
    print(f'Saved: {output.resolve()}')
    if not contours:
        print('No contours passed the filter. Try a smaller --min-area or lower Canny thresholds.')

    if not args.no_display:
        preview = np.hstack((image, colored))
        scale = min(1.0, 1500 / preview.shape[1], 900 / preview.shape[0])
        preview = cv2.resize(preview, None, fx=scale, fy=scale,
                             interpolation=cv2.INTER_AREA)
        title = 'Input | Colored contours (smallest on top) - press any key to close'
        cv2.imshow(title, preview)
        cv2.waitKey(0)
        cv2.destroyAllWindows()


if __name__ == '__main__':
    main()

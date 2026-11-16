#!/usr/bin/env python3
"""Check actual display pixels, not just Java view measurements (requires Pillow)."""
import argparse
import json
from PIL import Image


def pixels(image):
    data = iter(image.tobytes())
    return zip(data, data, data)


def check(path, stage):
    colors = [(255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 0, 255)]
    heights = [240, 720, 360, 480]
    image = Image.open(path).convert('RGB')
    expected = colors[stage]
    mask = Image.new('L', image.size)
    mask.putdata([255 if all(abs(a - b) < 45 for a, b in zip(pixel, expected)) else 0
                  for pixel in pixels(image)])
    bounds = mask.getbbox()
    if bounds is None:
        raise AssertionError('No dialog pixels for stage ' + str(stage))
    left, top, right, bottom = bounds
    width, height = right - left, bottom - top
    # JPEG edge ringing can displace the detected boundary by a few pixels.
    assert abs(width - 600) <= 4, (stage, bounds, 'width')
    assert abs(height - heights[stage]) <= 4, (stage, bounds, 'height')
    if stage == 3:
        assert left <= 2 and top <= 2, (stage, bounds, 'origin')
    else:
        assert abs(left + right - image.width) <= 4, (stage, bounds, 'horizontal center')
        assert abs(top + bottom - image.height) <= 4, (stage, bounds, 'vertical center')
    # A uniform old buffer stretched to the right layer size would pass above.
    # The bottom marker must be drawn at its new location without being stretched.
    crop = image.crop(bounds)
    marker = Image.new('L', crop.size)
    marker.putdata([255 if r > 210 and g > 210 and b < 45 else 0
                    for r, g, b in pixels(crop)])
    yellow = marker.getbbox()
    target = (30, heights[stage] - 60, 570, heights[stage] - 30)
    assert yellow is not None and all(abs(a - b) <= 4 for a, b in zip(yellow, target)), (
        stage, yellow, 'bottom marker clipped or scaled')
    return {'stage': stage, 'display': list(image.size), 'bounds': list(bounds),
            'bottomMarker': list(yellow), 'passed': True}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('captures', nargs=4, help='Display captures for stages 0, 1, 2, 3, in order')
    args = parser.parse_args()
    print(json.dumps([check(path, stage) for stage, path in enumerate(args.captures)], indent=2))

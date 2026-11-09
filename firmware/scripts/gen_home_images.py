#!/usr/bin/env python3
"""Rasterises the home screen artwork into LVGL ARGB8888 images (main/home/home_images.c).

Shapes match the design mock-up (Sophia the owl and the goal stars). Pure standard library:
every pixel is supersampled 4 x 4 against a list of filled shapes, painted back to front.
Run: python3 scripts/gen_home_images.py
"""
import math
import os

SS = 4  # supersampling per axis


def hex_rgb(h):
    return int(h[1:3], 16), int(h[3:5], 16), int(h[5:7], 16)


def in_ellipse(cx, cy, rx, ry):
    return lambda x, y: ((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2 <= 1.0


def in_polygon(pts):
    def test(x, y):
        inside = False
        j = len(pts) - 1
        for i in range(len(pts)):
            xi, yi = pts[i]
            xj, yj = pts[j]
            if (yi > y) != (yj > y) and x < (xj - xi) * (y - yi) / (yj - yi) + xi:
                inside = not inside
            j = i
        return inside
    return test


def near_polyline(pts, half_width):
    segs = list(zip(pts, pts[1:] + pts[:1]))

    def dist(px, py, a, b):
        ax, ay = a
        bx, by = b
        dx, dy = bx - ax, by - ay
        t = max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy)))
        return math.hypot(px - ax - t * dx, py - ay - t * dy)

    return lambda x, y: min(dist(x, y, a, b) for a, b in segs) <= half_width


def render(w, h, scale, shapes):
    """shapes: list of (test(x, y) in design units, '#rrggbb', alpha 0..1). Returns BGRA bytes."""
    out = bytearray()
    for py in range(h):
        for px in range(w):
            r = g = b = a = 0.0
            for sy in range(SS):
                for sx in range(SS):
                    x = (px + (sx + 0.5) / SS) / scale
                    y = (py + (sy + 0.5) / SS) / scale
                    cr = cg = cb = ca = 0.0
                    for test, color, alpha in shapes:
                        if test(x, y):
                            sr, sg, sb = hex_rgb(color)
                            # "over" compositing, straight alpha
                            na = alpha + ca * (1 - alpha)
                            if na > 0:
                                cr = (sr * alpha + cr * ca * (1 - alpha)) / na
                                cg = (sg * alpha + cg * ca * (1 - alpha)) / na
                                cb = (sb * alpha + cb * ca * (1 - alpha)) / na
                            ca = na
                    r += cr * ca
                    g += cg * ca
                    b += cb * ca
                    a += ca
            n = SS * SS
            if a > 0:
                r, g, b = r / a, g / a, b / a
            out += bytes((round(b), round(g), round(r), round(a / n * 255)))
    return bytes(out)


def owl():
    # 92 x 100 design units, see the mock-up's inline SVG
    return [
        (in_ellipse(46, 94, 30, 5), '#F1E3CA', 1.0),
        (in_polygon([(22, 30), (14, 8), (34, 20)]), '#7C3AED', 1.0),
        (in_polygon([(70, 30), (78, 8), (58, 20)]), '#7C3AED', 1.0),
        (in_ellipse(46, 56, 36, 36), '#8B5CF6', 1.0),
        (in_ellipse(46, 66, 22, 22), '#EDE4FF', 1.0),
        (in_ellipse(32, 44, 13, 13), '#FFFFFF', 1.0),
        (in_ellipse(60, 44, 13, 13), '#FFFFFF', 1.0),
        (in_ellipse(34, 45, 6.5, 6.5), '#1F2340', 1.0),
        (in_ellipse(58, 45, 6.5, 6.5), '#1F2340', 1.0),
        (in_ellipse(36, 42.5, 2.2, 2.2), '#FFFFFF', 1.0),
        (in_ellipse(60, 42.5, 2.2, 2.2), '#FFFFFF', 1.0),
        (in_polygon([(41, 56), (51, 56), (46, 63)]), '#F59E0B', 1.0),
        (in_ellipse(22, 60, 5, 5), '#F9A8D4', 1.0),
        (in_ellipse(70, 60, 5, 5), '#F9A8D4', 1.0),
    ]


STAR = [(12, 2.8), (14.8, 8.6), (21.1, 9.5), (16.5, 13.9), (17.6, 20.2),
        (12, 17.2), (6.4, 20.2), (7.5, 13.9), (2.9, 9.5), (9.2, 8.6)]


def star(fill, stroke):
    return [(in_polygon(STAR), fill, 1.0), (near_polyline(STAR, 0.7), stroke, 1.0)]


def write_png(path, w, h, bgra):
    """Writes straight-alpha BGRA pixels as an RGBA PNG (pure standard library)."""
    import struct
    import zlib

    raw = bytearray()
    for y in range(h):
        raw.append(0)
        for x in range(w):
            b, g, r, a = bgra[(y * w + x) * 4:(y * w + x) * 4 + 4]
            raw += bytes((r, g, b, a))

    def chunk(tag, data):
        return struct.pack('>I', len(data)) + tag + data + struct.pack('>I', zlib.crc32(tag + data) & 0xffffffff)

    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)) +
                chunk(b'IDAT', zlib.compress(bytes(raw), 9)) + chunk(b'IEND', b''))


def shifted(shapes, dx, dy):
    """The same shapes moved by (dx, dy) design units."""
    return [((lambda t, dx=dx, dy=dy: (lambda x, y: t(x - dx, y - dy)))(test), color, alpha)
            for test, color, alpha in shapes]


def c_array(name, w, h, data):
    lines = [f'static const uint8_t {name}_map[] = {{']
    for i in range(0, len(data), 24):
        lines.append('    ' + ', '.join(f'0x{v:02x}' for v in data[i:i + 24]) + ',')
    lines.append('};')
    lines.append(f'''
const lv_image_dsc_t {name} = {{
    .header = {{
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_ARGB8888,
        .flags = 0,
        .w = {w},
        .h = {h},
        .stride = {w * 4},
    }},
    .data_size = sizeof({name}_map),
    .data = {name}_map,
}};
''')
    return '\n'.join(lines)


def main():
    root = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'main', 'home')
    images = [
        ('home_img_owl', 92, 100, 1.0, owl()),
        ('home_img_star_on', 22, 22, 22 / 24, star('#FACC15', '#CA8A04')),
        ('home_img_star_off', 22, 22, 22 / 24, star('#F3EBDD', '#E2D5BF')),
    ]
    parts = ['// Generated by scripts/gen_home_images.py. Do not edit.',
             '#include "home_images.h"', '']
    for name, w, h, scale, shapes in images:
        parts.append(c_array(name, w, h, render(w, h, scale, shapes)))
    with open(os.path.join(root, 'home_images.c'), 'w') as f:
        f.write('\n'.join(parts))
    print('wrote home_images.c')


if __name__ == '__main__':
    main()

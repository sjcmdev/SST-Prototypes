import argparse
import csv
import struct
import zlib
from pathlib import Path


def read_columns(path):
    with open(path, newline="", encoding="utf-8") as handle:
        reader = csv.reader(handle, delimiter="\t")
        header = next(reader)
        columns = {name: [] for name in header}
        for row in reader:
            for index, name in enumerate(header):
                if index < len(row) and row[index]:
                    columns[name].append(float(row[index]))
    return columns


def triple(columns, prefix):
    keys = [key for key in columns if key.startswith(prefix + "_")]
    if len(keys) < 3:
        return [], [], []
    return columns[keys[0]], columns[keys[1]], columns[keys[2]]


def rgb(hex_color):
    value = hex_color.lstrip("#")
    if len(value) < 6:
        return (255, 255, 255)
    return tuple(int(value[index:index + 2], 16) for index in (0, 2, 4))


def write_png(path, width, height, pixels):
    def chunk(name, data):
        payload = name + data
        return struct.pack(">I", len(data)) + payload + struct.pack(">I", zlib.crc32(payload) & 0xFFFFFFFF)

    raw = bytearray()
    stride = width * 3
    for y in range(height):
        raw.append(0)
        raw.extend(pixels[y * stride:(y + 1) * stride])
    data = b"".join([
        b"\x89PNG\r\n\x1a\n",
        chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)),
        chunk(b"IDAT", zlib.compress(bytes(raw), 9)),
        chunk(b"IEND", b""),
    ])
    Path(path).write_bytes(data)


def fallback_png(args, columns):
    classes = [
        ("2_3_sigma", args.color3),
        ("1_2_sigma", args.color2),
        ("le_1_sigma", args.color1),
    ]
    all_x = []
    all_y = []
    triples = []
    for prefix, color in classes:
        x, y, _ = triple(columns, prefix)
        triples.append((x, y, rgb(color)))
        all_x.extend(x)
        all_y.extend(y)
    if not all_x or not all_y:
        raise SystemExit("No scatter points to plot")

    x_min, x_max = (args.xlim if args.xlim else (min(all_x), max(all_x)))
    y_min, y_max = (args.ylim if args.ylim else (min(all_y), max(all_y)))
    if x_min == x_max:
        x_min -= 0.5
        x_max += 0.5
    if y_min == y_max:
        y_min -= 0.5
        y_max += 0.5
    if not args.xlim:
        pad = (x_max - x_min) * 0.06
        x_min -= pad
        x_max += pad
    if not args.ylim:
        pad = (y_max - y_min) * 0.06
        y_min -= pad
        y_max += pad

    width, height = 1200, 1000
    left, right, top, bottom = 95, 45, 45, 85
    pixels = bytearray([28, 25, 23] * width * height)

    def put(x, y, color):
        if 0 <= x < width and 0 <= y < height:
            offset = (y * width + x) * 3
            pixels[offset:offset + 3] = bytes(color)

    def line(x0, y0, x1, y1, color):
        dx = abs(x1 - x0)
        sx = 1 if x0 < x1 else -1
        dy = -abs(y1 - y0)
        sy = 1 if y0 < y1 else -1
        err = dx + dy
        while True:
            put(x0, y0, color)
            if x0 == x1 and y0 == y1:
                break
            e2 = 2 * err
            if e2 >= dy:
                err += dy
                x0 += sx
            if e2 <= dx:
                err += dx
                y0 += sy

    def px(x):
        return int(left + (x - x_min) / (x_max - x_min) * (width - left - right))

    def py(y):
        return int(height - bottom - (y - y_min) / (y_max - y_min) * (height - top - bottom))

    for i in range(6):
        x = left + int(i * (width - left - right) / 5)
        y = top + int(i * (height - top - bottom) / 5)
        line(x, top, x, height - bottom, (65, 61, 58))
        line(left, y, width - right, y, (65, 61, 58))
    line(left, top, left, height - bottom, (125, 125, 125))
    line(left, height - bottom, width - right, height - bottom, (125, 125, 125))
    line(width - right, top, width - right, height - bottom, (80, 80, 80))
    line(left, top, width - right, top, (80, 80, 80))

    for x_values, y_values, color in triples:
        for x, y in zip(x_values, y_values):
            cx, cy = px(x), py(y)
            for oy in (-1, 0, 1):
                for ox in (-1, 0, 1):
                    put(cx + ox, cy + oy, color)

    Path(args.output).parent.mkdir(parents=True, exist_ok=True)
    write_png(args.output, width, height, pixels)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--title", required=True)
    parser.add_argument("--xlabel", required=True)
    parser.add_argument("--ylabel", required=True)
    parser.add_argument("--color1", default="#FF1F14FF")
    parser.add_argument("--color2", default="#FFE60FFF")
    parser.add_argument("--color3", default="#00FFE8FF")
    parser.add_argument("--xlim", nargs=2, type=float)
    parser.add_argument("--ylim", nargs=2, type=float)
    args = parser.parse_args()

    columns = read_columns(args.input)
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except Exception:
        fallback_png(args, columns)
        return

    fig, ax = plt.subplots(figsize=(8, 7), dpi=150)
    for prefix, label, color in [
        ("2_3_sigma", "2-3 sigma", args.color3),
        ("1_2_sigma", "1-2 sigma", args.color2),
        ("le_1_sigma", "<= 1 sigma", args.color1),
    ]:
        x, y, _ = triple(columns, prefix)
        if x and y:
            ax.scatter(x, y, s=8, c=color, label=label, edgecolors="none")

    ax.set_title(args.title)
    ax.set_xlabel(args.xlabel)
    ax.set_ylabel(args.ylabel)
    ax.grid(True, which="both", alpha=0.35)
    ax.tick_params(axis="both", which="both", direction="in", top=True, right=True)
    if args.xlim:
        ax.set_xlim(args.xlim)
    if args.ylim:
        ax.set_ylim(args.ylim)
    ax.legend(frameon=True)
    Path(args.output).parent.mkdir(parents=True, exist_ok=True)
    fig.tight_layout()
    fig.savefig(args.output)
    plt.close(fig)


if __name__ == "__main__":
    main()

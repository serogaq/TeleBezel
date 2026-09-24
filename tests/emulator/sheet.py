#!/usr/bin/env python3
import struct
import sys
import zlib

GAP = 6
BACKGROUND = 0x80


def chunks(data):
    position = 8
    while position < len(data):
        (length,) = struct.unpack(">I", data[position:position + 4])
        yield data[position + 4:position + 8], data[position + 8:position + 8 + length]
        position += 12 + length


def read(path):
    with open(path, "rb") as source:
        data = source.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        sys.exit(f"{path} is not a PNG")
    width = height = 0
    compressed = b""
    for kind, body in chunks(data):
        if kind == b"IHDR":
            width, height, depth, colour = struct.unpack(">IIBB", body[:10])
            if depth != 8 or colour != 2:
                sys.exit(f"{path} is not 8-bit RGB")
        elif kind == b"IDAT":
            compressed += body
    raw = zlib.decompress(compressed)
    stride = width * 3 + 1
    rows = []
    for row in range(height):
        if raw[row * stride] != 0:
            sys.exit(f"{path} uses PNG filters; only screenshots from control.py are supported")
        rows.append(raw[row * stride + 1:(row + 1) * stride])
    return width, height, rows


def write(path, width, height, rows):
    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)

    raw = b"".join(b"\x00" + bytes(row) for row in rows)
    with open(path, "wb") as target:
        target.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)) +
                     chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


def sheet(output, columns, paths):
    images = [read(path) for path in paths]
    cell_width = max(image[0] for image in images)
    cell_height = max(image[1] for image in images)
    columns = max(1, min(columns, len(images)))
    lines = (len(images) + columns - 1) // columns
    width = columns * cell_width + (columns - 1) * GAP
    height = lines * cell_height + (lines - 1) * GAP
    canvas = [bytearray([BACKGROUND]) * (width * 3) for _ in range(height)]
    for index, (image_width, image_height, rows) in enumerate(images):
        left = (index % columns) * (cell_width + GAP)
        top = (index // columns) * (cell_height + GAP)
        for row in range(image_height):
            canvas[top + row][left * 3:(left + image_width) * 3] = rows[row]
    write(output, width, height, canvas)


if __name__ == "__main__":
    if len(sys.argv) < 4:
        sys.exit("usage: sheet.py OUTPUT COLUMNS IMAGE...")
    sheet(sys.argv[1], int(sys.argv[2]), sys.argv[3:])

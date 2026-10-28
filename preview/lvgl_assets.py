"""Decodifica imagens exportadas pelo SquareLine (src/ui/assets/ui_img_*.c) em PNG.

Sem dependencias externas: le o array C, converte ARGB8888 (BGRA em memoria),
RGB565 ou I4 (indexado) para RGBA e escreve o PNG na mao.
"""

import re
import struct
import zlib
from pathlib import Path

_DATA_RE = re.compile(r"_data\[\]\s*=\s*\{(.*?)\};", re.S)
_BYTE_RE = re.compile(r"0x([0-9A-Fa-f]{2})")
_DSC_RE = re.compile(
    r"const\s+lv_image_dsc_t\s+(\w+)\s*=\s*\{(.*?)\};", re.S
)

# I4: 16 entradas de lv_color32_t antes dos indices.
_I4_PALETTE = 16 * 4


def _field(body, name):
    m = re.search(r"\.header\.%s\s*=\s*(\w+)" % name, body)
    if not m:
        raise ValueError("campo .header.%s nao encontrado" % name)
    return m.group(1)


def _png(width, height, rgba):
    def chunk(tag, data):
        c = tag + data
        return struct.pack(">I", len(data)) + c + struct.pack(">I", zlib.crc32(c))

    raw = b"".join(
        b"\x00" + rgba[y * width * 4:(y + 1) * width * 4] for y in range(height)
    )
    return (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(raw, 9))
        + chunk(b"IEND", b"")
    )


def decode(path):
    """Retorna (nome_do_dsc, width, height, png_bytes)."""
    path = Path(path)
    src = path.read_text(errors="replace")

    dsc = _DSC_RE.search(src)
    if not dsc:
        raise ValueError("%s: lv_image_dsc_t nao encontrado" % path.name)
    name, body = dsc.group(1), dsc.group(2)

    cf = _field(body, "cf")
    if cf not in (
        "LV_COLOR_FORMAT_ARGB8888",
        "LV_COLOR_FORMAT_RGB565",
        "LV_COLOR_FORMAT_I4",
    ):
        raise ValueError("%s: formato %s ainda nao suportado" % (path.name, cf))

    width, height = int(_field(body, "w")), int(_field(body, "h"))

    data = _DATA_RE.search(src)
    if not data:
        raise ValueError("%s: array de pixels nao encontrado" % path.name)
    buf = bytes(int(b, 16) for b in _BYTE_RE.findall(data.group(1)))

    if cf == "LV_COLOR_FORMAT_I4":
        # Paleta de 16 lv_color32_t (B,G,R,A) seguida dos indices, 2 px por byte:
        # nibble alto e o pixel da esquerda. As linhas andam de .header.stride em
        # .header.stride, que pode ter padding alem de (width + 1) // 2.
        stride = int(_field(body, "stride"))
        need = _I4_PALETTE + stride * height
        if len(buf) < need:
            raise ValueError(
                "%s: %d bytes lidos, %d esperados para %dx%d"
                % (path.name, len(buf), need, width, height)
            )

        palette = buf[:_I4_PALETTE]
        rgba = bytearray(width * height * 4)
        for y in range(height):
            row = _I4_PALETTE + y * stride
            for x in range(width):
                byte = buf[row + (x >> 1)]
                idx = (byte >> 4) if x % 2 == 0 else (byte & 0x0F)
                src_i, dst_i = 4 * idx, 4 * (y * width + x)
                rgba[dst_i] = palette[src_i + 2]          # R
                rgba[dst_i + 1] = palette[src_i + 1]      # G
                rgba[dst_i + 2] = palette[src_i]          # B
                rgba[dst_i + 3] = palette[src_i + 3]      # A
        return name, width, height, _png(width, height, bytes(rgba))

    bpp = 2 if cf == "LV_COLOR_FORMAT_RGB565" else 4
    need = width * height * bpp
    if len(buf) < need:
        raise ValueError(
            "%s: %d bytes lidos, %d esperados para %dx%d"
            % (path.name, len(buf), need, width, height)
        )

    rgba = bytearray(width * height * 4)
    if bpp == 2:
        # RGB565 little-endian, sempre opaco.
        for i in range(width * height):
            v = buf[2 * i] | (buf[2 * i + 1] << 8)
            r, g, b = (v >> 11) & 0x1F, (v >> 5) & 0x3F, v & 0x1F
            rgba[4 * i] = (r << 3) | (r >> 2)
            rgba[4 * i + 1] = (g << 2) | (g >> 4)
            rgba[4 * i + 2] = (b << 3) | (b >> 2)
            rgba[4 * i + 3] = 0xFF
    else:
        # LVGL guarda ARGB8888 como B,G,R,A em little-endian.
        rgba[0::4] = buf[2:need:4]
        rgba[1::4] = buf[1:need:4]
        rgba[2::4] = buf[0:need:4]
        rgba[3::4] = buf[3:need:4]

    return name, width, height, _png(width, height, bytes(rgba))


def decode_dir(assets_dir):
    """Mapeia {nome_do_dsc: {w, h, png}} para todos os ui_img_*.c."""
    out = {}
    for c_file in sorted(Path(assets_dir).glob("ui_img_*.c")):
        name, w, h, png = decode(c_file)
        out[name] = {"w": w, "h": h, "png": png, "source": c_file.name}
    if not out:
        raise ValueError("nenhum ui_img_*.c encontrado em %s" % assets_dir)
    return out

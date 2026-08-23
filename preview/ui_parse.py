"""Le src/ui/views/*.c e extrai o layout das telas para o preview.

A ideia e nao duplicar coordenadas: o firmware continua sendo a unica fonte de
verdade. Se um regex aqui parar de casar, o build falha com o arquivo e o
padrao que quebrou, em vez de mostrar um preview desatualizado.
"""

import re
from pathlib import Path


class ParseError(RuntimeError):
    pass


def _read(path):
    p = Path(path)
    if not p.exists():
        raise ParseError("arquivo nao encontrado: %s" % p)
    return p.read_text(errors="replace")


def _one(src, pattern, what, path):
    m = re.search(pattern, src)
    if not m:
        raise ParseError("%s: nao consegui ler %s (padrao: %s)" % (path, what, pattern))
    return m


def _hex(value):
    return "#%06X" % int(value, 16)


def _int_attr(src, func, obj, what, path):
    return int(_one(src, r"%s\(\s*%s\s*,\s*(-?\d+)" % (func, obj), what, path).group(1))


def _label(src, obj, path):
    """Coordenadas, cor e fonte de um lv_label_create nomeado."""
    color = _one(
        src,
        r"lv_obj_set_style_text_color\(\s*%s\s*,\s*lv_color_hex\(0x([0-9A-Fa-f]{6})\)" % obj,
        "cor de %s" % obj,
        path,
    ).group(1)
    font = _one(
        src,
        r"lv_obj_set_style_text_font\(\s*%s\s*,\s*&lv_font_montserrat_(\d+)" % obj,
        "fonte de %s" % obj,
        path,
    ).group(1)
    return {
        "id": obj,
        "x": _int_attr(src, "lv_obj_set_x", obj, "x de %s" % obj, path),
        "y": _int_attr(src, "lv_obj_set_y", obj, "y de %s" % obj, path),
        "color": _hex(color),
        "font": int(font),
    }


def _screen_bg(src, screen, path):
    m = _one(
        src,
        r"lv_obj_set_style_bg_color\(\s*%s\s*,\s*lv_color_hex\(0x([0-9A-Fa-f]{6})\)" % screen,
        "cor de fundo de %s" % screen,
        path,
    )
    return _hex(m.group(1))


def _image(src, path):
    return _one(src, r"lv_image_set_src\(\s*\w+\s*,\s*&(\w+)\)", "imagem de fundo", path).group(1)


def parse_splash(views_dir):
    path = Path(views_dir) / "ui_splash.c"
    src = _read(path)
    return {"image": _image(src, path), "bg": _screen_bg(src, "ui_splash", path)}


def parse_nano(views_dir):
    path = Path(views_dir) / "ui_nanomode.c"
    src = _read(path)
    labels = [
        _label(src, obj, path)
        for obj in re.findall(r"(ui_\w+)\s*=\s*lv_label_create", src)
    ]
    if not labels:
        raise ParseError("%s: nenhum label encontrado" % path)
    return {
        "image": _image(src, path),
        "bg": _screen_bg(src, "ui_nanomode", path),
        "labels": labels,
    }


def _helper_body(src, signature, path):
    start = src.find(signature)
    if start < 0:
        raise ParseError("%s: helper %s nao encontrado" % (path, signature))
    depth, i = 0, src.index("{", start)
    for j in range(i, len(src)):
        if src[j] == "{":
            depth += 1
        elif src[j] == "}":
            depth -= 1
            if depth == 0:
                return src[i:j]
    raise ParseError("%s: helper %s sem fechamento" % (path, signature))


def parse_track(views_dir):
    path = Path(views_dir) / "ui_trackmode.c"
    src = _read(path)

    gauge_body = _helper_body(src, "static lv_obj_t * _gauge(", path)
    gauge_style = {
        "font": int(_one(gauge_body, r"&lv_font_montserrat_(\d+)", "fonte do gauge", path).group(1)),
        "color": _hex(_one(gauge_body, r"lv_obj_set_style_text_color\(lbl,\s*lv_color_hex\(0x([0-9A-Fa-f]{6})\)", "cor do gauge", path).group(1)),
    }

    max_body = _helper_body(src, "static void _max_container(", path)
    max_style = {
        "font": int(_one(max_body, r"&lv_font_montserrat_(\d+)", "fonte do max", path).group(1)),
        "color": _hex(_one(max_body, r"lv_obj_set_style_text_color\(\*val_lbl,\s*lv_color_hex\(0x([0-9A-Fa-f]{6})\)", "cor do max", path).group(1)),
        "h": int(_one(max_body, r"lv_obj_set_height\(\*cont,\s*(\d+)\)", "altura do max", path).group(1)),
        # o valor entra com deslocamento fixo; o texto "max" usa lbl3_x da chamada
        "val_dx": int(_one(max_body, r"lv_obj_set_x\(\*val_lbl,\s*(-?\d+)\)", "dx do valor", path).group(1)),
        "label_dy": int(_one(max_body, r"lv_obj_set_y\(\*max_lbl,\s*(-?\d+)\)", "dy do texto max", path).group(1)),
        "text": _one(max_body, r'lv_label_set_text\(\*max_lbl,\s*"([^"]*)"', "texto do max", path).group(1),
        # LV_FLEX_FLOW_ROW_REVERSE: o valor fica a direita do texto "max"
        "reverse": "LV_FLEX_FLOW_ROW_REVERSE" in max_body,
    }

    gauges = [
        {"id": g[0], "x": int(g[1]), "y": int(g[2])}
        for g in re.findall(r"(ui_\w+)\s*=\s*_gauge\(\s*ui_trackmode\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*\)", src)
    ]
    maxes = [
        {"id": m[0], "x": int(m[1]), "y": int(m[2]), "w": int(m[3]), "label_dx": int(m[4])}
        for m in re.findall(
            r"_max_container\(\s*ui_trackmode\s*,\s*&\w+\s*,\s*&(\w+)\s*,\s*&\w+\s*,"
            r"\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(\d+)\s*,\s*(-?\d+)",
            src,
        )
    ]
    if len(gauges) != len(maxes) or not gauges:
        raise ParseError(
            "%s: %d gauges para %d blocos de max" % (path, len(gauges), len(maxes))
        )

    for g, m in zip(gauges, maxes):
        g.update(gauge_style)
        g["max"] = dict(max_style, **m)

    return {"image": _image(src, path), "bg": _screen_bg(src, "ui_trackmode", path), "gauges": gauges}


def parse_ble(views_dir):
    path = Path(views_dir) / "ui_blelist.c"
    src = _read(path)

    def define(name):
        return int(_one(src, r"#define\s+%s\s+(\d+)" % name, name, path).group(1))

    style = _helper_body(src, "static void apply_item_style(", path)
    colors = re.findall(r"lv_color_hex\(0x([0-9A-Fa-f]{6})\)", style)
    if len(colors) < 2:
        raise ParseError("%s: cores dos itens da lista nao encontradas" % path)

    size = _one(src, r"lv_obj_set_size\(list_scroll,\s*(\d+),\s*240\s*-\s*BLELIST_PAD_TOP", "largura da lista", path)
    empty = re.findall(r'lv_label_set_text\(empty,\s*bt_state\.scanning\s*\?\s*"([^"]*)"\s*:\s*"([^"]*)"', src)

    return {
        "image": _image(src, path),
        "bg": _screen_bg(src, "ui_blelist", path),
        "item_h": define("BLELIST_ITEM_H"),
        "gap": define("BLELIST_ITEM_GAP"),
        "pad_top": define("BLELIST_PAD_TOP"),
        "width": int(size.group(1)),
        "color_connected": _hex(colors[0]),
        "color_idle": _hex(colors[1]),
        "radius": int(_one(style, r"lv_obj_set_style_radius\(item,\s*(\d+)", "raio do item", path).group(1)),
        "pad_hor": int(_one(style, r"lv_obj_set_style_pad_hor\(item,\s*(\d+)", "padding do item", path).group(1)),
        "text_scanning": empty[0][0] if empty else "Buscando dispositivos...",
        "text_empty": empty[0][1] if empty else "Nenhum dispositivo encontrado",
    }


def parse_navigation(ui_dir):
    path = Path(ui_dir) / "ui_navigation.h"
    src = _read(path)
    c_path = Path(ui_dir) / "ui_navigation.c"
    c_src = _read(c_path)
    return {
        "splash_ms": int(_one(src, r"SPLASH_DURATION_MS\s+(\d+)", "SPLASH_DURATION_MS", path).group(1)),
        "transition_ms": int(_one(src, r"SCREEN_TRANSITION_MS\s+(\d+)", "SCREEN_TRANSITION_MS", path).group(1)),
        "swipe_px": int(_one(c_src, r"SWIPE_MIN_PX\s+(\d+)", "SWIPE_MIN_PX", c_path).group(1)),
        "order": re.findall(r"&(ui_\w+)", _one(c_src, r"nav_screens\[\]\s*=\s*\{([^}]*)\}", "ordem das telas", c_path).group(1)),
    }


def parse_all(src_dir):
    src_dir = Path(src_dir)
    ui_dir, views = src_dir / "ui", src_dir / "ui" / "views"
    return {
        "splash": parse_splash(views),
        "nano": parse_nano(views),
        "track": parse_track(views),
        "ble": parse_ble(views),
        "nav": parse_navigation(ui_dir),
        "mock": parse_mock(ui_dir),
    }


def parse_mock(ui_dir):
    """Mapeia cada label -> formatador e sinal, lendo ui_mock_data.c."""
    path = Path(ui_dir) / "ui_mock_data.c"
    src = _read(path)
    body = _helper_body(src, "static void render_gauges(", path)

    binds = {}
    for fmt, obj, expr in re.findall(r"fmt_(f1|f2|int)\(\s*(ui_\w+)\s*,\s*(.+?)\);", body):
        # "(int)g->water_temp_c" -> water_temp_c ; "max_boost <= X ? 0 : max_boost" -> max_boost
        expr = re.sub(r"\(int\)|\(float\)", "", expr).strip()
        signal = re.match(r"(?:g->)?(\w+)", expr)
        if not signal:
            raise ParseError("%s: nao entendi a expressao '%s'" % (path, expr))
        binds[obj] = {"fmt": fmt, "signal": signal.group(1)}

    if not binds:
        raise ParseError("%s: nenhum fmt_* encontrado em render_gauges()" % path)

    default = _one(
        src,
        r'lv_label_set_text\(labels\[i\],\s*"([^"]*)"\)',
        "valor padrao dos gauges",
        path,
    ).group(1)

    return {"binds": binds, "default": default}

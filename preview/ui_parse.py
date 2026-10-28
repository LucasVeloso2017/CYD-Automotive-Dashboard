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
    if not gauges:
        raise ParseError("%s: nenhum gauge encontrado" % path)
    for g in gauges:
        g.update(gauge_style)

    # nem todo gauge tem bloco de max (a lambda nao tem): cada max vai para o
    # gauge mais proximo, em vez de parear por ordem
    for m in maxes:
        g = min(gauges, key=lambda g: abs(g["x"] - m["x"]) + abs(g["y"] - m["y"]))
        if "max" in g:
            raise ParseError("%s: dois blocos de max para o gauge %s" % (path, g["id"]))
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
        # Um &ui_* por linha da lista nav_screens[]; blocos "#if FLAG" de ui.h
        # que estao em 0 saem, como sairiam no pre-processador.
        "order": re.findall(
            r"^\s*&(ui_\w+)",
            _drop_disabled(
                _one(c_src, r"(?s)nav_screens\[\]\s*=\s*\{(.*?)\n\};", "ordem das telas", c_path).group(1),
                ui_dir,
            ),
            re.M,
        ),
    }


def _ui_flag(ui_dir, name):
    """Valor de um #define de flag em ui.h (UI_ENABLE_SPLASH, UI_ENABLE_WEB...)."""
    path = Path(ui_dir) / "ui.h"
    return _one(_read(path), r"#define\s+%s\s+(\d+)" % name, name, path).group(1) != "0"


def _drop_disabled(text, ui_dir):
    """Remove os trechos "#if UI_ENABLE_X ... #endif" cuja flag esta em 0."""
    def keep(m):
        return m.group(2) if _ui_flag(ui_dir, m.group(1)) else ""
    return re.sub(r"(?ms)^\s*#if\s+(UI_ENABLE_\w+)\s*$(.*?)^\s*#endif\b[^\n]*$", keep, text)


def parse_statusbar(ui_dir):
    """Alinhamento, margens, fonte e cor do icone de Bluetooth (ui_statusbar.c)."""
    path = Path(ui_dir) / "ui_statusbar.c"
    src = _read(path)

    def define(name):
        return int(_one(src, r"#define\s+%s\s+(?:0x)?([0-9A-Fa-f]+)" % name, name, path).group(1),
                   16 if "COLOR" in name else 10)

    return {
        "margin_x": define("STATUSBAR_MARGIN_X"),
        "margin_y": define("STATUSBAR_MARGIN_Y"),
        "font": int(_one(src, r"&lv_font_montserrat_(\d+)", "fonte da barra", path).group(1)),
        "color_bt": "#%06X" % define("STATUSBAR_COLOR_BT"),
        "align": _one(src, r"#define\s+STATUSBAR_ALIGN\s+LV_ALIGN_TOP_(LEFT|MID|RIGHT)", "STATUSBAR_ALIGN", path).group(1).lower(),
    }

def parse_dtc(views_dir):
    """Todos os #define DTC_* de ui_dtc.c (geometria e cores) e os textos fixos."""
    path = Path(views_dir) / "ui_dtc.c"
    src = _read(path)
    vals = {}
    for name, val in re.findall(r"#define\s+DTC_(\w+)\s+(0x[0-9A-Fa-f]+|\d+)", src):
        vals[name.lower()] = ("#%06X" % int(val, 16)) if name.startswith("COLOR_") else int(val, 0)
    for need in ("list_y", "list_h", "btn_y", "btn_h", "pad_x", "color_stored"):
        if need not in vals:
            raise ParseError("%s: DTC_%s nao encontrado" % (path, need.upper()))
    title = _one(src, r'text\(ui_dtc,\s*"([^"]*)"', "titulo da tela DTC", path).group(1)

    # textos da descricao estrutural, lidos do C para nao divergirem
    dpath = Path(views_dir).parent.parent / "gateway" / "dtc_describe.c"
    dsrc = _read(dpath)
    systems_body = _one(dsrc, r"SYSTEMS\[4\]\s*=\s*\{([^}]*)\}", "SYSTEMS", dpath).group(1)
    systems = re.findall(r'"([^"]*)"', systems_body)
    psub = {}
    for cases, text in re.findall(r"((?:case\s+0x[0-9A-Fa-f]+:\s*)+)return\s+\"([^\"]*)\"", dsrc):
        for h in re.findall(r"0x([0-9A-Fa-f]+)", cases):
            psub[h.upper()] = text
    if len(systems) != 4 or len(psub) < 10:
        raise ParseError("%s: textos da descricao estrutural nao encontrados" % dpath)
    return dict(vals, title=title, systems=systems, psub=psub)


def parse_eth(views_dir):
    """#define ETH_* de ui_etanol.c (geometria, faixas e cores) e o titulo."""
    path = Path(views_dir) / "ui_etanol.c"
    src = _read(path)
    vals = {}
    for name, val in re.findall(r"#define\s+ETH_(\w+)\s+\(?(-?0x[0-9A-Fa-f]+|-?\d+)\)?", src):
        vals[name.lower()] = ("#%06X" % int(val, 16)) if name.startswith("COLOR_") else int(val, 0)
    for need in ("title_y", "value_y", "bar_w", "bar_h", "bar_y", "scale_y", "class_y",
                 "gasolina_max", "etanol_min", "color_accent"):
        if need not in vals:
            raise ParseError("%s: ETH_%s nao encontrado" % (path, need.upper()))
    title = _one(src, r'text\(ui_etanol,\s*"([^"]*)"', "titulo da tela de etanol", path).group(1)
    return dict(vals, title=title)


def parse_cfg(views_dir):
    """#define CFG_* de ui_config.c (geometria e cores) e os textos fixos."""
    path = Path(views_dir) / "ui_config.c"
    src = _read(path)
    vals = {}
    for name, val in re.findall(r"#define\s+CFG_(\w+)\s+(0x[0-9A-Fa-f]+|\d+)", src):
        vals[name.lower()] = ("#%06X" % int(val, 16)) if name.startswith("COLOR_") else int(val, 0)
    for need in ("title_y", "card_x", "card_w", "card_pad", "bright_y", "bright_h",
                 "rotate_y", "rotate_h", "slider_h", "switch_w", "switch_h", "footer_y"):
        if need not in vals:
            raise ParseError("%s: CFG_%s nao encontrado" % (path, need.upper()))
    texts = re.findall(r'text\(\w+,\s*"([^"]+)"', src)
    if len(texts) < 4:
        raise ParseError("%s: textos da tela de configuracoes nao encontrados" % path)
    return dict(vals, title=texts[0], bright=texts[1], rotate=texts[2], footer=texts[3])


def splash_enabled(ui_dir):
    """Le UI_ENABLE_SPLASH de ui.h. O preview le texto, nao passa pelo
    pre-processador: sem isto a splash apareceria mesmo fora do build."""
    path = Path(ui_dir) / "ui.h"
    return _one(_read(path), r"#define\s+UI_ENABLE_SPLASH\s+(\d+)", "UI_ENABLE_SPLASH", path).group(1) != "0"


def parse_all(src_dir):
    src_dir = Path(src_dir)
    ui_dir, views = src_dir / "ui", src_dir / "ui" / "views"
    return {
        "splash": parse_splash(views) if splash_enabled(ui_dir) else None,
        "nano": parse_nano(views),
        "track": parse_track(views),
        "ble": parse_ble(views),
        "dtc": parse_dtc(views),
        "eth": parse_eth(views),
        "cfg": parse_cfg(views),
        "nav": parse_navigation(ui_dir),
        "mock": parse_mock(ui_dir),
        "statusbar": parse_statusbar(ui_dir),
    }


def parse_mock(ui_dir):
    """Mapeia cada label -> formatador e sinal, lendo ui_mock_data.c."""
    path = Path(ui_dir) / "ui_mock_data.c"
    src = _read(path)
    body = _helper_body(src, "static void render_gauges(", path)

    # Os gauges sao desenhados suavizados: render_gauges() le "sm_boost.shown",
    # nao "g->boost_bar". O vinculo entre os dois esta nos smooth_set() do
    # poll_cb, entao e de la que ele sai -- o preview continua derivando tudo do
    # codigo, sem tabela paralela para alguem esquecer de atualizar.
    smoothed = dict(re.findall(r"smooth_set\(&(\w+),\s*g->(\w+)\)", src))

    binds = {}
    for fmt, obj, expr in re.findall(
        r"fmt_(f1|f2|int)\(\s*\w+\s*,\s*(ui_\w+)\s*,\s*(.+?)\);", body
    ):
        # "(int)sm_water.shown" -> water_temp_c ; "max_boost <= X ? 0 : max_boost" -> max_boost
        expr = re.sub(r"\(int\)|\(float\)", "", expr).strip()
        smooth = re.match(r"(\w+)\.shown\b", expr)
        if smooth:
            signal = smoothed.get(smooth.group(1))
            if not signal:
                raise ParseError(
                    "%s: '%s' nao tem smooth_set() correspondente" % (path, smooth.group(1))
                )
        else:
            m = re.match(r"(?:g->)?(\w+)", expr)
            if not m:
                raise ParseError("%s: nao entendi a expressao '%s'" % (path, expr))
            signal = m.group(1)
        binds[obj] = {"fmt": fmt, "signal": signal}

    if not binds:
        raise ParseError("%s: nenhum fmt_* encontrado em render_gauges()" % path)

    default = _one(
        src,
        r'set_text\(slots\[i\],\s*labels\[i\],\s*"([^"]*)"\)',
        "valor padrao dos gauges",
        path,
    ).group(1)

    return {"binds": binds, "default": default}

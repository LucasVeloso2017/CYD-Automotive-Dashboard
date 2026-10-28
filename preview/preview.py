#!/usr/bin/env python3
"""Preview no navegador das telas do Car Dashboard Remastered.

    python3 preview/preview.py            # gera, serve e abre o navegador
    python3 preview/preview.py --build    # so gera preview/dist/index.html
    python3 preview/preview.py --port 9000 --no-open

Nada e copiado a mao: os fundos saem dos assets LVGL e as coordenadas,
fontes, cores e formatacoes saem dos proprios .c das views. Cada request
regenera a pagina, entao basta recarregar depois de editar o firmware.
"""

import argparse
import base64
import json
import sys
import webbrowser
from datetime import datetime
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from lvgl_assets import decode_dir           # noqa: E402
from ui_parse import ParseError, parse_all   # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
PREVIEW = ROOT / "preview"
DIST = PREVIEW / "dist"
TEMPLATE = PREVIEW / "template.html"
SAMPLE = PREVIEW / "sample_data.json"
MARKER = "/*__DATA__*/"


def build():
    """Gera dist/index.html e devolve o caminho."""
    layout = parse_all(ROOT / "src")
    assets = decode_dir(ROOT / "src" / "ui" / "assets")
    sample = json.loads(SAMPLE.read_text(encoding="utf-8"))

    used = {
        layout[screen]["image"]
        for screen in ("splash", "nano", "track", "ble")
        if layout.get(screen)                       # splash pode estar fora do build
    }
    missing = used - set(assets)
    if missing:
        raise ParseError("assets ausentes em src/ui/assets: %s" % ", ".join(sorted(missing)))

    images = {
        name: {
            "w": a["w"],
            "h": a["h"],
            "data": "data:image/png;base64," + base64.b64encode(a["png"]).decode(),
        }
        for name, a in assets.items()
        if name in used
    }

    data = {
        "layout": layout,
        "images": images,
        "signals": sample["signals"],
        "devices": sample["devices"],
        "dtc": sample.get("dtc", {}),
        "meta": {
            "generated": datetime.now().strftime("%d/%m/%Y %H:%M"),
            "source": "src/ui/views/*.c + src/ui/assets/ui_img_*.c",
        },
    }

    template = TEMPLATE.read_text(encoding="utf-8")
    head, _, rest = template.partition(MARKER)
    _, _, tail = rest.partition(MARKER)
    if not tail:
        raise ParseError("template.html: marcadores %s nao encontrados" % MARKER)

    DIST.mkdir(exist_ok=True)
    out = DIST / "index.html"
    out.write_text(head + json.dumps(data, ensure_ascii=False) + tail, encoding="utf-8")
    return out


class Handler(SimpleHTTPRequestHandler):
    def do_GET(self):
        if self.path in ("/", "/index.html"):
            try:
                build()
            except Exception as exc:  # mostra o erro na pagina em vez de servir algo velho
                body = ("<!doctype html><meta charset='utf-8'>"
                        "<body style='font:14px ui-monospace,monospace;padding:24px;"
                        "background:#7F1D1D;color:#fff'><b>Falha ao gerar o preview</b>"
                        "<pre>%s</pre></body>" % exc).encode("utf-8")
                self.send_response(500)
                self.send_header("Content-Type", "text/html; charset=utf-8")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)
                return
        return super().do_GET()

    def end_headers(self):
        self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def log_message(self, fmt, *args):
        pass


def main():
    ap = argparse.ArgumentParser(description="Preview das telas do CYD no navegador")
    ap.add_argument("--port", type=int, default=8000)
    ap.add_argument("--build", action="store_true", help="apenas gera dist/index.html")
    ap.add_argument("--no-open", action="store_true", help="nao abre o navegador")
    args = ap.parse_args()

    try:
        out = build()
    except Exception as exc:
        print("erro: %s" % exc, file=sys.stderr)
        return 1
    print("gerado: %s" % out.relative_to(ROOT))

    if args.build:
        return 0

    url = "http://localhost:%d/" % args.port
    server = ThreadingHTTPServer(("127.0.0.1", args.port), partial(Handler, directory=str(DIST)))
    print("servindo %s  (ctrl+c para sair)" % url)
    if not args.no_open:
        webbrowser.open(url)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print()
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    sys.exit(main())

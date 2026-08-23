# Preview no navegador

Roda as telas do dashboard no computador, em escala 2×, antes de gravar no CYD.

```sh
python3 preview/preview.py            # gera, sobe em http://localhost:8000 e abre o navegador
python3 preview/preview.py --build    # só gera preview/dist/index.html
python3 preview/preview.py --port 9000 --no-open
```

Só precisa de Python 3.8+ — nenhuma dependência externa.

## O que ele mostra

- as quatro telas (`splash`, `nanomode`, `trackmode`, `blelist`) com os fundos reais;
- swipe arrastando com o mouse, com o mesmo limiar de `SWIPE_MIN_PX` (setas ← → também);
- alternância entre desconectado (gauges em `00`) e conectado, com os valores de
  `sample_data.json` formatados exatamente como no firmware;
- grade de pixels opcional, para ver como o texto cai na malha de 320×240.

## Por que ele não desatualiza

Nada de coordenada copiada à mão. A cada build o preview lê os próprios fontes:

| O que | De onde |
| --- | --- |
| Imagens de fundo | `src/ui/assets/ui_img_*.c` (ARGB8888 → PNG) |
| Posição, fonte e cor dos labels | `src/ui/views/*.c` |
| Itens da lista BLE (altura, gap, cores, raio) | `ui_blelist.c` |
| Ordem das telas, swipe, tempo do splash | `ui_navigation.c/.h` |
| Qual sinal alimenta cada label e com qual formato | `ui_mock_data.c` (`fmt_f1`/`fmt_f2`/`fmt_int`) |

O servidor regera a página a cada request: edite um `.c`, recarregue o navegador.
Se algum regex deixar de casar depois de uma refatoração, o build falha dizendo qual
arquivo e qual padrão quebrou — melhor do que mostrar um preview mentindo.

## Valores de exemplo

`preview/sample_data.json` guarda os valores **brutos** dos sinais de `vehicle_data.h`
e a lista de dispositivos Bluetooth. A formatação é aplicada pelo preview, em
aritmética de 32 bits (`Math.fround`), igual à do ESP32 — por isso `max_lambda: 1.05`
aparece como `1.04` e `max_comb: 6.1` como `6.0`, exatamente como apareceria na tela.

## Limites

É uma aproximação em HTML, não um emulador de LVGL. As fontes são a Montserrat web,
não os bitmaps `lv_font_montserrat_*`, então a largura do texto varia alguns pixels.
Transições de tela e o fade do splash são aproximados em CSS. Touch, Bluetooth e
OBD2 não são simulados.

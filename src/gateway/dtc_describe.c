#include "dtc_describe.h"

#include <stddef.h>

static const char *const SYSTEMS[4] = {
    "Motor e transmissao",   /* P */
    "Chassi",                /* C */
    "Carroceria",            /* B */
    "Rede entre modulos",    /* U */
};

/* Terceiro caractere dos codigos P genericos (P0xxx e P2xxx): o subsistema. */
static const char *powertrain_subsystem(uint8_t d2)
{
    switch (d2) {
    case 0x0: return "Ar/combustivel e emissoes auxiliares";
    case 0x1: return "Dosagem de ar e combustivel";
    case 0x2: return "Dosagem de combustivel, injetores";
    case 0x3: return "Ignicao e falha de combustao";
    case 0x4: return "Controle de emissoes (EGR, catalisador, EVAP)";
    case 0x5: return "Velocidade, marcha lenta e entradas auxiliares";
    case 0x6: return "Modulo de controle e circuitos de saida";
    case 0x7: case 0x8: case 0x9: return "Transmissao";
    case 0xA: case 0xB: case 0xC: return "Propulsao hibrida";
    default:  return NULL;
    }
}

void dtc_describe(uint16_t raw, dtc_info_t *out)
{
    uint8_t sys = (raw >> 14) & 0x03;   /* P C B U */
    uint8_t d1  = (raw >> 12) & 0x03;   /* 0-3 */
    uint8_t d2  = (raw >> 8)  & 0x0F;
    uint8_t d3  = (raw >> 4)  & 0x0F;
    uint8_t d4  =  raw        & 0x0F;

    out->system = SYSTEMS[sys];
    out->subsystem = NULL;
    out->cylinder = 0;

    /* Quem define o significado, pelo segundo caractere (J2012):
     *   P: 0 e 2 SAE, 1 fabricante, 3 dividido (P3000-P33FF fabricante, P3400+ SAE)
     *   C/B/U: 0 SAE, 1 e 2 fabricante, 3 reservado ao SAE */
    if (sys == 0) out->generic = d1 == 0 || d1 == 2 || (d1 == 3 && d2 >= 0x4);
    else          out->generic = d1 == 0 || d1 == 3;

    if (sys == 0 && (d1 == 0 || d1 == 2)) out->subsystem = powertrain_subsystem(d2);

    /* P0300 aleatorio; P0301-P0312 = cilindro (digitos decimais nos nibbles) */
    if (sys == 0 && d1 == 0 && d2 == 0x3 && d3 <= 1 && d4 <= 9) {
        int cyl = d3 * 10 + d4;
        if (cyl == 0)       out->cylinder = -1;
        else if (cyl <= 12) out->cylinder = (int8_t)cyl;
    }
}

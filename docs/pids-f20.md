# PIDs OBD-II — BMW F20 (DME 0x12)

Levantamento feito em 2026-10-16 pelo ENET (HSFZ, gateway `169.254.75.25:6801`),
VIN `98M1U9000G4A23085`, motor em marcha lenta, aquecendo.

Todas as PIDs abaixo sao do **modo 01** do SAE J1979 e foram confirmadas pelo
bitmap de suporte da DME (`0100` → `BE3FA813`, `0120` → `A007B011`,
`0140` → `FED0C410`). Nenhuma PID a partir de `0x61` e publicada.

## Resumo

| Parametro            | PID    | Comando ELM327 | Bytes | Formula                          | Unidade | Lido   |
|----------------------|--------|----------------|-------|----------------------------------|---------|--------|
| Sonda lambda (pre-cat) | `0x34` | `0134`       | A B C D | `(256A+B) × 2/65536`           | λ       | 1,044  |
| Lambda comandado     | `0x44` | `0144`         | A B   | `(256A+B) × 2/65536`             | λ       | 1,001  |
| Sonda pos-cat        | `0x15` | `0115`         | A B   | `A / 200`                        | V       | 0,055  |
| Temp. da agua        | `0x05` | `0105`         | A     | `A − 40`                         | °C      | 57     |
| Tensao da bateria    | `0x42` | `0142`         | A B   | `(256A+B) / 1000`                | V       | 14,30  |
| Pressao do turbo     | `0x0B` − `0x33` | `010B` e `0133` | A / A | `(MAP − Baro) / 100`  | bar     | −0,05  |
| Pressao de combustivel | `0x23` | `0123`       | A B   | `(256A+B) × 10 / 100`            | bar     | 104,9  |
| Temp. de admissao (IAT) | `0x0F` | `010F`      | A     | `A − 40`                         | °C      | 28     |
| Temp. do oleo        | `0x5C` | `015C`         | A     | `A − 40`                         | °C      | 45     |
| Etanol no combustivel | `0x52` | `0152`        | A     | `A × 100 / 255`                  | %       | 29,4   |
| Tipo de combustivel  | `0x51` | `0151`         | A     | tabela J1979 (`1` = gasolina)    | —       | 1      |

## Detalhes

### Sonda lambda — `0x34`
Sonda de banda larga, banco 1 sensor 1 (antes do catalisador).
- `A B`: razao de equivalencia, `(256A+B) × 2/65536` (1,000 = estequiometrico).
- `C D`: corrente da sonda, `(256C+D)/256 − 128` mA.
- AFR de gasolina pura: `λ × 14,7`. Para E27 brasileira use ~13,2; para etanol, ~9,0.
- `0x24`–`0x2B` (formato antigo de banda larga) **nao** sao suportadas neste carro.
- `0x44` e o lambda que a DME esta *pedindo*; serve para comparar com `0x34`.
- `0x15` e a sonda de banda estreita depois do catalisador (0–1,1 V); o byte B
  (`0xFF`) indica que ela nao entra no trim de combustivel.

### Temperatura da agua — `0x05`
`A − 40` °C. Faixa −40 a 215 °C.

### Tensao da bateria — `0x42`
Tensao de alimentacao da DME (`(256A+B)/1000` V), nao a do sensor IBS da bateria.
Na pratica fica a ~0,1–0,2 V da tensao no borne. Com o motor ligado mostra a
carga do alternador (~14,3 V).

### Pressao do turbo — `0x0B` e `0x33`
Nao ha PID dedicada: a `0x70` (pressao de turbo) nao e suportada pela DME.
- `0x0B`: pressao absoluta do coletor (MAP), `A` kPa.
- `0x33`: pressao barometrica, `A` kPa.
- Boost em bar: `(MAP − Baro) / 100`. Valor negativo e vacuo.
- **Limite**: `0x0B` tem um byte so, satura em 255 kPa absolutos (~1,55 bar de
  boost ao nivel do mar). Acima disso a leitura trava no teto.
- O motor tem Valvetronic, entao em marcha lenta o coletor fica perto da pressao
  atmosferica (≈ 0 bar), e nao em vacuo profundo como num motor de borboleta.

### Pressao de combustivel — `0x23`
Pressao do rail de injecao direta (alta pressao), manometrica:
`(256A+B) × 10` kPa → `÷ 100` para bar. Em marcha lenta, ~105 bar.
- `0x0A` (bomba de baixa) e `0x22` (rail relativo ao vacuo) **nao** sao suportadas.
- `0x59` (rail absoluta) **nao** e suportada.

### Temperatura de admissao — `0x0F`
`A − 40` °C.

### Temperatura do oleo — `0x5C`
`A − 40` °C. Suportada pela DME (bit de `0x5C` em `0140`), entao nao precisa de
DID proprio da BMW.

### Etanol no combustivel — `0x52`
`A × 100/255` %, resolucao de ~0,4 %. Lido 29,4 % com gasolina comum
(E30 desde ago/2025). O valor e o que a DME considera para o calculo de
injecao; depois de abastecer pode levar alguns km para convergir.
A `0x51` informa `1` (gasolina): a tabela J1979 nao tem codigo de "flex",
entao nao serve para saber se o carro e flex.

## Como o firmware le

O gateway ([elm327_gateway.cpp](../src/gateway/elm327_gateway.cpp)) usa o que
foi medido aqui:

- **Varios PIDs por pedido.** A DME responde a ate 6 PIDs num pedido so, no
  mesmo tempo de um (~30 ms pelo ENET). Cada pedido leva as grandezas rapidas
  (`0x0B`, `0x23`, `0x34`) e completa os 3 slots livres com as lentas em rodizio
  (`0x05`, `0x42`, `0x0F`, `0x5C`, `0x52`, `0x33`): cada lenta vem a cada 2 pedidos.
- **So a DME.** `ATSH7E0` endereca a DME, entao o pedido pode terminar com a
  contagem de respostas (`010B23340F42051`) e o ELM327 devolve assim que ela
  responde, sem esperar o timeout. `ATAT2` encurta o timeout quando a contagem
  nao esta em uso. Leitura e apagamento de DTC voltam ao broadcast (`ATSH7DF`).
- **Escada de modos.** Se o adaptador cortar o multi-frame ou nao responder 3
  vezes seguidas, desce para multi-PID sem contagem e, depois, um PID por vez.
  O modo e a taxa real (pedidos/s) saem no serial a cada 5 s.

## Como pedir

**ELM327** (porta OBD, D-CAN 500 kbit/s, ISO 15765-4 11 bit):
```
ATSP6        protocolo CAN 11 bit 500k
ATSH7E0      enderecar so a DME (opcional; sem isso vai em broadcast 7DF)
0105         → 41 05 61
```

**ENET** (HSFZ/TCP 6801): payload `F4 12 01 05` → resposta `12 F4 41 05 61`.
Os bytes de dados sao identicos aos do ELM327.

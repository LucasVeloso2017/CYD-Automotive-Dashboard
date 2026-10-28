// Teste do protocolo serial CYD <-> Wemos no PC, sem hardware.
//
//   cd preview && make -f test_link.mk
//
// O protocolo existe em duas copias (uma por sketch, Arduino nao compila
// arquivo de fora da pasta). A primeira verificacao e que elas sao iguais
// byte a byte; depois, que um quadro corrompido nunca passa e que cada
// conteudo volta exatamente como foi.

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

#include "link_proto.h"

static int g_fail = 0;
#define CHECK(cond, ...) do { if (!(cond)) { g_fail++; printf("  FALHOU: " __VA_ARGS__); printf("\n"); } } while (0)

static std::string slurp(const char *path)
{
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

int main(void)
{
    printf("== as duas copias do protocolo sao identicas ==\n");
    const std::string cyd = slurp("../src/net/link_proto.h");
    const std::string wemos = slurp("../wemos-portal/src/link_proto.h");
    CHECK(!cyd.empty() && cyd == wemos,
          "src/net/link_proto.h e wemos-portal/src/link_proto.h divergiram: copie um sobre o outro");

    printf("== selar e conferir ==\n");
    char b[128];
    strcpy(b, "S{\"a\":1}");
    int n = link_seal(b, strlen(b), sizeof(b));
    CHECK(n > 0 && b[n - 1] == '\n' && b[n - 4] == '*', "quadro mal fechado: %s", b);
    char copy[128];
    memcpy(copy, b, (size_t)n + 1);
    int body = link_verify(copy, (size_t)n);
    CHECK(body == 8 && strcmp(copy, "S{\"a\":1}") == 0, "o conteudo devia voltar intacto, veio %s", copy);

    printf("== qualquer byte trocado derruba o quadro ==\n");
    int caught = 0, tried = 0;
    for (int i = 0; i < n - 1; i++) {           /* tudo menos o '\n' */
        for (int bit = 0; bit < 8; bit++) {
            memcpy(copy, b, (size_t)n + 1);
            copy[i] ^= (char)(1 << bit);
            if (copy[i] == '\n' || copy[i] == '\r') continue;   /* vira fim de linha: outro caso */
            tried++;
            if (link_verify(copy, (size_t)n) < 0) caught++;
        }
    }
    CHECK(caught == tried, "%d de %d bits trocados passaram pela verificacao", tried - caught, tried);

    printf("== linhas invalidas ==\n");
    strcpy(copy, "S{}");
    CHECK(link_verify(copy, strlen(copy)) < 0, "sem checksum");
    strcpy(copy, "*00");
    CHECK(link_verify(copy, strlen(copy)) < 0, "sem conteudo");
    strcpy(copy, "S{}*G1");
    CHECK(link_verify(copy, strlen(copy)) < 0, "checksum com caractere nao hex");
    CHECK(link_verify(NULL, 10) < 0, "linha nula");
    char small[8] = "S12";
    CHECK(link_seal(small, 3, 7) < 0, "selar sem espaco devia falhar");
    CHECK(link_seal(small, 3, 8) > 0, "com espaco exato devia caber");

    printf("== CRLF no fim tambem e aceito ==\n");
    strcpy(b, "R");
    n = link_seal(b, 1, sizeof(b));
    b[n - 1] = '\r'; b[n] = '\n'; b[n + 1] = '\0';
    CHECK(link_verify(b, (size_t)n + 1) == 1 && b[0] == 'R', "CRLF devia ser tolerado");

    printf("== amostra G: ida e volta ==\n");
    link_sample_t s = {};
    s.valid = 1;
    s.v[LINK_F_LAMBDA] = 0.987f;
    s.v[LINK_F_WATER]  = 91.4f;
    s.v[LINK_F_BATT]   = 13.86f;
    s.v[LINK_F_BOOST]  = -0.734f;     /* vacuo: negativo tem que sobreviver */
    s.v[LINK_F_COMB]   = 211.7f;
    s.v[LINK_F_IAT]    = -12.3f;
    s.v[LINK_F_OIL]    = NAN;         /* carro sem 0x5C */
    n = link_sample_format(b, sizeof(b), &s);
    CHECK(n > 0, "formatar a amostra falhou");
    printf("  quadro: %s\n", b);
    link_sample_t r = {};
    CHECK(link_sample_parse(b, &r) == 0, "a amostra formatada devia ser lida");
    const float tol[LINK_F_COUNT] = { 0.0006f, 0.06f, 0.006f, 0.0006f, 0.06f, 0.06f, 0.06f };
    for (int i = 0; i < LINK_F_COUNT - 1; i++)
        CHECK(std::fabs(r.v[i] - s.v[i]) <= tol[i], "campo %d: %.4f virou %.4f", i, (double)s.v[i], (double)r.v[i]);
    CHECK(std::isnan(r.v[LINK_F_OIL]), "sem leitura devia voltar NaN");
    CHECK(r.valid == 1, "valid se perdeu");

    printf("== amostra G malformada ==\n");
    link_sample_t junk;
    CHECK(link_sample_parse("G1,1,2,3", &junk) < 0, "campos faltando");
    CHECK(link_sample_parse("G1,1,2,3,4,5,6,7,8", &junk) < 0, "campos sobrando");
    CHECK(link_sample_parse("G1,1,2,x,4,5,6,7", &junk) < 0, "lixo no numero");
    CHECK(link_sample_parse("S1,1,2,3,4,5,6,7", &junk) < 0, "tipo errado");
    CHECK(link_sample_parse("G1,1,2,-5,4,5,6,7", &junk) == 0 && std::fabs(junk.v[2] + 0.05f) < 1e-6f,
          "negativo de verdade nao pode ser confundido com o '-' de sem leitura");

    printf("== W: ida e volta ==\n");
    link_hello_t h = {};
    strcpy(h.ssid, "CarDash-299D");
    strcpy(h.pass, "dash33299D");
    strcpy(h.url, "http://192.168.4.1");
    h.clients = 2;
    n = link_hello_format(b, sizeof(b), &h);
    CHECK(n > 0, "formatar W falhou");
    link_hello_t hr = {};
    CHECK(link_hello_parse(b, &hr) == 0, "W formatado devia ser lido");
    CHECK(!strcmp(hr.ssid, h.ssid) && !strcmp(hr.pass, h.pass) && !strcmp(hr.url, h.url) && hr.clients == 2,
          "W nao voltou igual");

    printf("== W malformado ==\n");
    CHECK(link_hello_parse("Wa\tb\tc", &hr) < 0, "faltando aparelhos");
    CHECK(link_hello_parse("Wa\tb\tc\t1\textra", &hr) < 0, "campo sobrando");
    CHECK(link_hello_parse("Wa\tb\tc\t999", &hr) < 0, "aparelhos fora de 0..255");
    std::string longssid = "W" + std::string(40, 'x') + "\tp\tu\t1";
    CHECK(link_hello_parse(longssid.c_str(), &hr) < 0, "SSID maior que o buffer devia ser recusado, nao estourar");

    printf(g_fail ? "\n%d verificacao(oes) falharam\n" : "\nok\n", g_fail);
    return g_fail ? 1 : 0;
}

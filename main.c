#include <stdio.h>
#include "gera_codigo.h"

int main() {
    FILE *fp = fopen("teste.lbs", "r");
    unsigned char code[300];
    funcp func;
    int res;

    gera_codigo(fp, code, &func);
    fclose(fp);

    /* Dump primeiros bytes para debug */
    printf("Código gerado (hex):\n");
    for (int i = 0; i < 200; ++i) {
        printf("%02X ", code[i]);
        if ((i+1) % 16 == 0) printf("\n");
    }
    printf("\n\n");
    /* ...existing code... */

    if (func == NULL) {
        printf("Erro na geração de código.\n");
        return 1;
    }

    res = func(10);
    printf("Resultado: %d\n", res);
    return 0;
}

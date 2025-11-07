/*
 * gera_codigo.c
 * Implementação de gera_codigo para o 2o trabalho INF1018 - Software Básico
 *
 * Observações:
 * - NÃO incluir main neste arquivo (main apenas para testes locais).
 * - Esta implementação gera código x86-64 (sequências de bytes).
 * - Suporta:
 *     - prólogo/epílogo
 *     - ret $const, ret p0, ret vN
 *     - atribuições com var/p0/$const
 *     - operações +, -, *
 *     - call num varpc (calcula rel32)
 *     - zret a b (retorno condicional)
 *
 * Coloque aqui os nomes dos integrantes (substitua os placeholders):
 * /* Nome_do_Aluno1 Matrícula Turma */
  /* Nome_do_Aluno2 Matrícula Turma */
 

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "gera_codigo.h"

/* máx de funções esperadas no arquivo (segundo enunciado)
   (50 linhas no arquivo, então 50 funções é suficiente) */
#define MAX_FUNCS 64
#define MAX_LINE 200

/* Mapeamento de variáveis locais:
   v0 -> [rbp - 4]
   v1 -> [rbp - 8]
   ...
   v4 -> [rbp - 20]
   Reservamos espaço local_bytes (usamos 32 para alinhamento)
*/
static const int LOCAL_BYTES = 32;

/* helpers para escrever bytes no buffer code[] */
static void emit_bytes(unsigned char code[], int *idx, const unsigned char *b, int n) {
    memcpy(code + *idx, b, n);
    *idx += n;
}
static void emit_byte(unsigned char code[], int *idx, unsigned char b) {
    code[(*idx)++] = b;
}
static void emit_int32(unsigned char code[], int *idx, int val) {
    unsigned char b[4];
    b[0] = val & 0xFF;
    b[1] = (val >> 8) & 0xFF;
    b[2] = (val >> 16) & 0xFF;
    b[3] = (val >> 24) & 0xFF;
    emit_bytes(code, idx, b, 4);
}

/* patcha um int32 little-endian na posição pos do buffer */
static void patch_int32(unsigned char code[], int pos, int val) {
    code[pos + 0] = val & 0xFF;
    code[pos + 1] = (val >> 8) & 0xFF;
    code[pos + 2] = (val >> 16) & 0xFF;
    code[pos + 3] = (val >> 24) & 0xFF;
}

/* Emite prólogo:
   push rbp             -> 0x55
   mov rbp, rsp         -> 0x48 0x89 0xE5
   sub rsp, imm8        -> 0x48 0x83 0xEC imm8
*/
static void emit_prologue(unsigned char code[], int *idx) {
    unsigned char a[] = { 0x55 };
    unsigned char b[] = { 0x48, 0x89, 0xE5 };
    unsigned char c[] = { 0x48, 0x83, 0xEC };
    emit_bytes(code, idx, a, 1);
    emit_bytes(code, idx, b, 3);
    emit_bytes(code, idx, c, 3);
    emit_byte(code, idx, (unsigned char)LOCAL_BYTES);
}

/* Emite epílogo leave; ret -> 0xC9 0xC3 */
static void emit_epilogue(unsigned char code[], int *idx) {
    unsigned char e[] = { 0xC9, 0xC3 };
    emit_bytes(code, idx, e, 2);
}

/* Mov eax, imm32 -> B8 imm32 */
static void emit_mov_eax_imm(unsigned char code[], int *idx, int imm) {
    emit_byte(code, idx, 0xB8);
    emit_int32(code, idx, imm);
}

/* Mov eax, edi -> movl %edi, %eax  -> 0x89 0xF8 */
static void emit_mov_eax_edi(unsigned char code[], int *idx) {
    unsigned char b[] = { 0x89, 0xF8 };
    emit_bytes(code, idx, b, 2);
}

/* Mov ecx, imm32 -> B9 imm32 */
static void emit_mov_ecx_imm(unsigned char code[], int *idx, int imm) {
    emit_byte(code, idx, 0xB9);
    emit_int32(code, idx, imm);
}

/* Mov eax, [rbp - disp] : 8B 45 disp8  (disp >0 ; we store at rbp - disp) */
static void emit_mov_eax_mem_rbp_disp(unsigned char code[], int *idx, int disp) {
    unsigned char b[3];
    b[0] = 0x8B;
    /* 0x45 = modrm for [rbp - disp8] with reg = eax (000) -> 01 000 101 = 0x45 */
    b[1] = 0x45;
    b[2] = ((-disp) & 0xFF);
    emit_bytes(code, idx, b, 3);
}

/* Mov ecx, [rbp - disp] : 8B 4D disp8 (reg ecx=001 => 0x4D) */
static void emit_mov_ecx_mem_rbp_disp(unsigned char code[], int *idx, int disp) {
    unsigned char b[3];
    b[0] = 0x8B;
    b[1] = 0x4D; /* reg=ecx */
    b[2] = ((-disp) & 0xFF);
    emit_bytes(code, idx, b, 3);
}

/* Mov [rbp - disp], eax : 89 45 disp8 */
static void emit_mov_mem_rbp_disp_eax(unsigned char code[], int *idx, int disp) {
    unsigned char b[3];
    b[0] = 0x89;
    b[1] = 0x45;
    b[2] = ((-disp) & 0xFF);
    emit_bytes(code, idx, b, 3);
}

/* Mov [rbp - disp], ecx : 89 4D disp8 */
static void emit_mov_mem_rbp_disp_ecx(unsigned char code[], int *idx, int disp) {
    unsigned char b[3];
    b[0] = 0x89;
    b[1] = 0x4D;
    b[2] = ((-disp) & 0xFF);
    emit_bytes(code, idx, b, 3);
}

/* add eax, imm32 -> 05 imm32 */
static void emit_add_eax_imm(unsigned char code[], int *idx, int imm) {
    emit_byte(code, idx, 0x05);
    emit_int32(code, idx, imm);
}

/* sub eax, imm32 -> 2D imm32 */
static void emit_sub_eax_imm(unsigned char code[], int *idx, int imm) {
    emit_byte(code, idx, 0x2D);
    emit_int32(code, idx, imm);
}

/* add eax, ecx -> 01 C8 (add r/m32, r32) with r32=ecx, r/m32 = eax -> modrm C8 */
static void emit_add_eax_ecx(unsigned char code[], int *idx) {
    unsigned char b[] = { 0x01, 0xC8 };
    emit_bytes(code, idx, b, 2);
}

/* sub eax, ecx -> 29 C8 */
static void emit_sub_eax_ecx(unsigned char code[], int *idx) {
    unsigned char b[] = { 0x29, 0xC8 };
    emit_bytes(code, idx, b, 2);
}

/* imul eax, ecx -> 0F AF C1 (imul r32, r/m32 with reg=eax (000) rm=ecx (001) => 0xC1) */
static void emit_imul_eax_ecx(unsigned char code[], int *idx) {
    unsigned char b[] = { 0x0F, 0xAF, 0xC1 };
    emit_bytes(code, idx, b, 3);
}

/* call rel32 -> E8 imm32 (imm32 = target_offset - next_instr_offset) */
static void emit_call_rel32(unsigned char code[], int *idx, int target_offset) {
    int call_next = (*idx) + 5; /* next instruction virtual offset (within buffer) */
    int rel = target_offset - call_next;
    emit_byte(code, idx, 0xE8);
    emit_int32(code, idx, rel);
}

/* test eax,eax -> 85 C0 (mais robusto para verificar zero) */
static void emit_cmp_eax_zero(unsigned char code[], int *idx) {
    unsigned char b[] = { 0x85, 0xC0 };
    emit_bytes(code, idx, b, 2);
}
/* jne rel32 -> 0F 85 imm32 (usado para zret: se !=0 pula sobre o retorno imediato) */
static int emit_jne_rel32_placeholder(unsigned char code[], int *idx) {
    unsigned char b[] = { 0x0F, 0x85, 0x00, 0x00, 0x00, 0x00 };
    int pos = *idx;
    emit_bytes(code, idx, b, 6);
    return pos; /* posição onde imm32 será patchada (pos+2) */
}

/* util parse: trim leading/trailing whitespace */
static void trim(char *s) {
    if (!s) return;
    int i = 0;
    while (isspace((unsigned char)s[i])) i++;
    if (i) memmove(s, s + i, strlen(s + i) + 1);
    int len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) s[--len] = '\0';
}

/* parse signed integer from string (handles optional leading '+/-') */
static int parse_snum(const char *s) {
    return atoi(s);
}

/* Evaluate varpc and emit code to load it into EAX.
   varpc: "vN", "p0", or "$<num>"
*/
static void emit_load_varpc_to_eax(unsigned char code[], int *idx, const char *tok) {
    if (tok[0] == '$') {
        int imm = parse_snum(tok + 1);
        emit_mov_eax_imm(code, idx, imm);
    } else if (strcmp(tok, "p0") == 0) {
        emit_mov_eax_edi(code, idx);
    } else if (tok[0] == 'v') {
        int v = atoi(tok + 1);
        int disp = 4 * (v + 1);
        emit_mov_eax_mem_rbp_disp(code, idx, disp);
    } else {
        /* not expected; do nothing */
    }
}

/* Evaluate varpc and emit code to load it into ECX (useful for binary ops) */
static void emit_load_varpc_to_ecx(unsigned char code[], int *idx, const char *tok) {
    if (tok[0] == '$') {
        int imm = parse_snum(tok + 1);
        emit_mov_ecx_imm(code, idx, imm);
    } else if (strcmp(tok, "p0") == 0) {
        /* mov ecx, edi -> 89 F9 */
        unsigned char b[] = { 0x89, 0xF9 };
        emit_bytes(code, idx, b, 2);
    } else if (tok[0] == 'v') {
        int v = atoi(tok + 1);
        int disp = 4 * (v + 1);
        emit_mov_ecx_mem_rbp_disp(code, idx, disp);
    } else {
        /* not expected */
    }
}

/* main generator */
void gera_codigo(FILE *f, unsigned char code[], funcp *entry) {
    char line[MAX_LINE];
    int idx = 0; /* posição atual em code[] */

    int func_offsets[MAX_FUNCS];
    int func_count = 0;

    int current_func = -1;
    int last_was_ret = 0;

    /* inicializa func_offsets */
    for (int i = 0; i < MAX_FUNCS; ++i) func_offsets[i] = -1;

    while (fgets(line, sizeof(line), f)) {
        trim(line);
        if (strlen(line) == 0) continue;

        if (strcmp(line, "function") == 0) {
            /* início de função */
            if (func_count >= MAX_FUNCS) {
                /* Excedeu número máximo esperado de funções: ignorar restante */
                current_func = -1;
                continue;
            }
            current_func = func_count;
            func_offsets[func_count] = idx;
            func_count++;
            last_was_ret = 0;
            emit_prologue(code, &idx);
            continue;
        }

        if (strcmp(line, "end") == 0) {
            /* fim da função; só emite epílogo se último comando não foi ret (que já emite leave;ret) */
            if (!last_was_ret) {
                emit_epilogue(code, &idx);
            }
            current_func = -1;
            last_was_ret = 0;
            continue;
        }

        /* comandos dentro da função */
        /* Detecta ret */
        if (strncmp(line, "ret ", 4) == 0) {
            char arg[MAX_LINE];
            strcpy(arg, line + 4);
            trim(arg);
            if (arg[0] == '$') {
                int imm = parse_snum(arg + 1);
                emit_mov_eax_imm(code, &idx, imm);
            } else if (strcmp(arg, "p0") == 0) {
                emit_mov_eax_edi(code, &idx);
            } else if (arg[0] == 'v') {
                int v = atoi(arg + 1);
                int disp = 4 * (v + 1);
                emit_mov_eax_mem_rbp_disp(code, &idx, disp);
            } else {
                /* not expected */
            }
            emit_epilogue(code, &idx);
            last_was_ret = 1;
            continue;
        }

        /* zret a b */
        if (strncmp(line, "zret ", 5) == 0) {
            /* formato: zret varpc varpc */
            char copy[MAX_LINE];
            strcpy(copy, line + 5);
            trim(copy);
            char *t1 = strtok(copy, " \t");
            char *t2 = strtok(NULL, " \t");
            if (!t1 || !t2) continue;
            /* Avalia t1 em eax, compara com zero */
            emit_load_varpc_to_eax(code, &idx, t1);
            emit_cmp_eax_zero(code, &idx);
            /* emit placeholder JNE -> se diferente de zero pula sobre o bloco de retorno imediato */
            int jne_pos = emit_jne_rel32_placeholder(code, &idx);
            /* se não é zero, continue execução (fall-through) */
            /* agora geramos o bloco de retorno (o destino do je) imediatamente */
            int ret_block_pos = idx;
            /* colocar valor t2 em eax */
            emit_load_varpc_to_eax(code, &idx, t2);
            emit_epilogue(code, &idx);
            /* patch JNE para saltar SOBRE o bloco de retorno: usa idx (próxima instr após o bloco) */
            int next_after_block = idx; /* idx já aponta para instr após epílogo */
            int rel = next_after_block - (jne_pos + 6); /* destino = endereço após o bloco */
            patch_int32(code, jne_pos + 2, rel);
            /* note: zret não finaliza a função necessariamente (apenas condicional) */
            last_was_ret = 0; /* pois só retorna condicional */
            continue;
        }

        /* call num varpc */
        if (strncmp(line, "call ", 5) == 0) {
            /* formato: call NUM varpc */
            char copy[MAX_LINE];
            strcpy(copy, line + 5);
            trim(copy);
            char *nstr = strtok(copy, " \t");
            char *arg = strtok(NULL, " \t");
            if (!nstr || !arg) continue;
            int num = atoi(nstr);
            /* preparar argumento: carregar varpc em edi (p0 -> edi já; para vN/$const -> mov edi, imm / mov edi, [rbp-disp]) */
            if (arg[0] == '$') {
                int imm = parse_snum(arg + 1);
                /* mov edi, imm -> BF imm32 */
                emit_byte(code, &idx, 0xBF);
                emit_int32(code, &idx, imm);
            } else if (strcmp(arg, "p0") == 0) {
                /* já em edi; nada a fazer */
            } else if (arg[0] == 'v') {
                int v = atoi(arg + 1);
                int disp = 4 * (v + 1);
                unsigned char b[3];
                b[0] = 0x8B;
                b[1] = 0x7D; /* reg=edi */
                b[2] = ((-disp) & 0xFF);
                emit_bytes(code, &idx, b, 3);
            }
            /* Emite call rel32: precisa do offset da função num em buffer */
            int target = 0;
            if (num >= 0 && num < MAX_FUNCS && func_offsets[num] >= 0) {
                target = func_offsets[num];
            } else {
                /* se função não definida (improvável segundo enunciado), chama para offset 0 (vai falhar) */
                target = 0;
            }
            emit_call_rel32(code, &idx, target);
            continue;
        }

        /* atribuição: vN = expr (expr pode ser oper ou call) */
        char *eq = strchr(line, '=');
        if (eq) {
            char left[MAX_LINE];
            strncpy(left, line, eq - line);
            left[eq - line] = '\0';
            trim(left);
            char right[MAX_LINE];
            strcpy(right, eq + 1);
            trim(right);
            /* verifica se right é call */
            if (strncmp(right, "call ", 5) == 0) {
                /* formato: vN = call num varpc  */
                char tmp[MAX_LINE];
                strcpy(tmp, right + 5);
                trim(tmp);
                char *nstr = strtok(tmp, " \t");
                char *arg = strtok(NULL, " \t");
                if (!nstr || !arg) continue;
                int num = atoi(nstr);
                /* preparar argumento em edi */
                if (arg[0] == '$') {
                    int imm = parse_snum(arg + 1);
                    emit_byte(code, &idx, 0xBF); /* mov edi, imm -> BF imm32 */
                    emit_int32(code, &idx, imm);
                } else if (strcmp(arg, "p0") == 0) {
                    /* nothing (already in edi) */
                } else if (arg[0] == 'v') {
                    int v = atoi(arg + 1);
                    int disp = 4 * (v + 1);
                    unsigned char b[3];
                    b[0] = 0x8B;
                    b[1] = 0x7D; /* mov edi, [rbp-disp] */
                    b[2] = ((-disp) & 0xFF);
                    emit_bytes(code, &idx, b, 3);
                }
                /* call */
                int target = 0;
                if (num >= 0 && num < MAX_FUNCS && func_offsets[num] >= 0) {
                    target = func_offsets[num];
                } else {
                    target = 0;
                }
                emit_call_rel32(code, &idx, target);
                /* after call, return value in eax; store to left var */
                if (left[0] == 'v') {
                    int vl = atoi(left + 1);
                    int disp_l = 4 * (vl + 1);
                    emit_mov_mem_rbp_disp_eax(code, &idx, disp_l);
                } else {
                    /* not expected */
                }
                continue;
            }

            /* otherwise right pode ser oper: varpc op varpc   (op: + - *) */
            /* tokeniza */
            char tcopy[MAX_LINE];
            strcpy(tcopy, right);
            trim(tcopy);
            /* procurar operador (+ - *) */
            char *oppos = NULL;
            char op = 0;
            for (int i = 0; tcopy[i]; ++i) {
                if (tcopy[i] == '+' || tcopy[i] == '-' || tcopy[i] == '*') {
                    op = tcopy[i];
                    oppos = tcopy + i;
                    break;
                }
            }
            if (oppos) {
                /* separa operandos */
                char leftop[MAX_LINE], rightop[MAX_LINE];
                int leftlen = (int)(oppos - tcopy);
                strncpy(leftop, tcopy, leftlen);
                leftop[leftlen] = '\0';
                strcpy(rightop, oppos + 1);
                trim(leftop); trim(rightop);
                /* Carrega primeiro operando em eax */
                emit_load_varpc_to_eax(code, &idx, leftop);
                /* segundo operando: se for immediate e op +/- podemos usar add/sub imm,
                   se for immediate and op '*' então mov ecx, imm and imul eax, ecx
                   se for var/p0 then mov ecx, val and use add/sub with ecx or imul eax,ecx */
                if (op == '+') {
                    if (rightop[0] == '$') {
                        int imm = parse_snum(rightop + 1);
                        emit_add_eax_imm(code, &idx, imm);
                    } else {
                        emit_load_varpc_to_ecx(code, &idx, rightop);
                        emit_add_eax_ecx(code, &idx);
                    }
                } else if (op == '-') {
                    if (rightop[0] == '$') {
                        int imm = parse_snum(rightop + 1);
                        emit_sub_eax_imm(code, &idx, imm);
                    } else {
                        emit_load_varpc_to_ecx(code, &idx, rightop);
                        emit_sub_eax_ecx(code, &idx);
                    }
                } else if (op == '*') {
                    /* multiply: if immediate, mov ecx, imm; else mov ecx, operand; then imul eax, ecx */
                    emit_load_varpc_to_ecx(code, &idx, rightop);
                    emit_imul_eax_ecx(code, &idx);
                }
                /* armazenar resultado em left var (vN) */
                if (left[0] == 'v') {
                    int vl = atoi(left + 1);
                    int disp_l = 4 * (vl + 1);
                    emit_mov_mem_rbp_disp_eax(code, &idx, disp_l);
                } else {
                    /* left side não é var -> ignorar (não suportado) */
                }
                last_was_ret = 0;
                continue;
            }

            /* se right for apenas varpc (ex: v0 = p0 ou v0 = $5) */
            if (right[0] == '$' || right[0] == 'p' || right[0] == 'v') {
                emit_load_varpc_to_eax(code, &idx, right);
                if (left[0] == 'v') {
                    int vl = atoi(left + 1);
                    int disp_l = 4 * (vl + 1);
                    emit_mov_mem_rbp_disp_eax(code, &idx, disp_l);
                }
                last_was_ret = 0;
                continue;
            }

            /* caso não reconhecido: ignorar */
            continue;
        }

        /* se não reconheceu a linha: ignorar */
    } /* fim while fgets */

    /* Ao final, definir entry como início da última função criada */
    if (func_count > 0) {
        int last = func_count - 1;
        /* entry recebe ponteiro para code + offset */
        *entry = (funcp)(code + func_offsets[last]);
    } else {
        *entry = NULL;
    }
}

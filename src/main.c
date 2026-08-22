/* bC driver: parse .bas, emit C. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "codegen.h"

extern int yyparse(void);
extern FILE *yyin;
extern StmtList *bc_program;

static void usage(void)
{
    fprintf(stderr, "usage: bc [-o out.c] input.bas\n");
    exit(1);
}

int main(int argc, char **argv)
{
    const char *out = NULL;
    const char *inp = NULL;
    const char *dot;
    FILE *f;
    int i;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-o")) {
            if (++i >= argc) usage();
            out = argv[i];
        } else if (argv[i][0] == '-' && argv[i][1]) {
            usage();
        } else {
            inp = argv[i];
        }
    }
    if (!inp) usage();

    f = fopen(inp, "r");
    if (!f) {
        fprintf(stderr, "bC: cannot open '%s'\n", inp);
        return 1;
    }
    yyin = f;
    if (yyparse()) return 1;
    fclose(f);

    if (!out) {
        static char buf[1024];
        dot = strrchr(inp, '.');
        if (dot && dot != inp) {
            size_t n = (size_t)(dot - inp);
            if (n < sizeof buf) { memcpy(buf, inp, n); buf[n] = 0; out = buf; }
        }
        if (!out) { snprintf(buf, sizeof buf, "%s.c", inp); out = buf; }
        else strncat((char *)out, ".c", sizeof buf - strlen(out) - 1);
    }

    cg_init();
    return cg_run(bc_program, inp, out);
}

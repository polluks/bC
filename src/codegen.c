/* bC -- batari Basic to cc65-C transpiler. AST constructors + code generator. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "codegen.h"
#include "bc.tab.h"

static const char *g_src = "?";

char *bc_strdup(const char *s)
{
    char *r = malloc(strlen(s) + 1);
    strcpy(r, s);
    return r;
}
char *bc_strdup_lower(const char *s)
{
    char *r = bc_strdup(s), *p;
    for (p = r; *p; p++) if (*p >= 'A' && *p <= 'Z') *p += 32;
    return r;
}

void cg_fatal(int line, const char *fmt, ...)
{
    va_list ap;
    fprintf(stderr, "bC: %s:%d: error: ", g_src, line);
    va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap);
    fputc('\n', stderr);
    exit(1);
}
void cg_warn(int line, const char *fmt, ...)
{
    va_list ap;
    fprintf(stderr, "bC: %s:%d: warning: ", g_src, line);
    va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap);
    fputc('\n', stderr);
}

/* ---------------- AST constructors ---------------- */

Expr *ex_num(long v, int line)  { Expr *e = calloc(1, sizeof *e); e->type = EX_NUM; e->num = v; e->line = line; return e; }
Expr *ex_id(char *id, int line) { Expr *e = calloc(1, sizeof *e); e->type = EX_ID; e->id = id; e->line = line; return e; }
Expr *ex_bin(int op, Expr *a, Expr *b, int line)
{ Expr *e = calloc(1, sizeof *e); e->type = EX_BIN; e->op = op; e->a = a; e->b = b; e->line = line; return e; }
Expr *ex_un(int op, Expr *a, int line)
{ Expr *e = calloc(1, sizeof *e); e->type = EX_UN; e->op = op; e->a = a; e->line = line; return e; }
Expr *ex_call(char *id, ExprList *args, int line)
{ Expr *e = calloc(1, sizeof *e); e->type = EX_CALL; e->id = id; e->args = args; e->line = line; return e; }
Expr *ex_index(char *id, Expr *i, int line)
{ Expr *e = calloc(1, sizeof *e); e->type = EX_INDEX; e->id = id; e->a = i; e->line = line; return e; }
Expr *ex_bitread(Expr *v, Expr *bit, int line)
{ Expr *e = calloc(1, sizeof *e); e->type = EX_BITREAD; e->a = v; e->b = bit; e->line = line; return e; }
ExprList *exl_one(Expr *e) { ExprList *l = calloc(1, sizeof *l); l->e = e; return l; }
ExprList *exl_more(ExprList *l, Expr *e) { ExprList *n = calloc(1, sizeof *n), *p = l; n->e = e; while (p->next) p = p->next; p->next = n; return l; }

Cond *cd_val(Expr *e) { Cond *c = calloc(1, sizeof *c); c->type = CD_VAL; c->e = e; return c; }
Cond *cd_cmp(int op, Expr *a, Expr *b, int line)
{ Cond *c = calloc(1, sizeof *c); c->type = CD_CMP; c->cmpop = op; c->e = a; c->eright = b; return c; }
Cond *cd_log(int type, Cond *a, Cond *b) { Cond *c = calloc(1, sizeof *c); c->type = type; c->a = a; c->b = b; return c; }
Cond *cd_not(Cond *a) { Cond *c = calloc(1, sizeof *c); c->type = CD_NOT; c->a = a; return c; }

Stmt *st_new(int type, int line)
{ Stmt *s = calloc(1, sizeof *s); s->type = type; s->line = line; return s; }
StmtList *stl_one(Stmt *s) { StmtList *l = calloc(1, sizeof *l); l->s = s; return l; }
StmtList *stl_more(StmtList *l, Stmt *s)
{
    StmtList *n = calloc(1, sizeof *n), *p = l;
    n->s = s; while (p->next) p = p->next; p->next = n; return l;
}

/* ---------------- output buffer ---------------- */

typedef struct { char *s; size_t n, cap; } Out;
static Out g_pro, g_init, g_body;   /* declarations / init section / main body */

static void oput(Out *o, const char *s)
{
    size_t n = strlen(s);
    while (o->cap < o->n + n + 1) { o->cap = o->cap ? o->cap * 2 : 8192; o->s = realloc(o->s, o->cap); }
    memcpy(o->s + o->n, s, n); o->n += n; o->s[o->n] = 0;
}
static void oprintf(Out *o, const char *fmt, ...)
{
    char tmp[4096];
    va_list ap;
    va_start(ap, fmt); vsnprintf(tmp, sizeof tmp, fmt, ap); va_end(ap);
    oput(o, tmp);
}
static void olinef(Out *o, int indent, const char *fmt, ...)
{
    char tmp[4096];
    va_list ap;
    int i;
    va_start(ap, fmt); vsnprintf(tmp, sizeof tmp, fmt, ap); va_end(ap);
    for (i = 0; i < indent; i++) oput(o, "    ");
    oput(o, tmp);
    oput(o, "\n");
}

/* ---------------- symbol tables ---------------- */

typedef struct Dim {
    char *name;
    int is_addr;          /* dim x = $80 */
    long addr;
    char *base;           /* dim x = a */
    int indexed;          /* referenced with () anywhere */
    struct Dim *next;
} Dim;
typedef struct Con { char *name; long val; int done, busy; Expr *expr; struct Con *next; } Con;
typedef struct Lab { char *name; int defined, refs; struct Lab *next; } Lab;
typedef struct DT { char *name; unsigned char bytes[256]; int len; long start; int sread_used; struct DT *next; } DT;

static Dim *dims;
static Con *consts;
static Lab *labels;
static DT *dtables;

static int var_used[26];
static int temp_used[8];       /* temp1..temp7 */
static int have_scorecolor;

typedef struct GSITE { int id; } GSITE;
static int n_gosubs;

enum { LP_WHILE, LP_DO, LP_FOR };
typedef struct LoopCtx {
    int type, uid;
    char *var;
    struct LoopCtx *up;
} LoopCtx;
static LoopCtx *loops;
static int uid_counter;

static int tv_pal;
static int have_pfcolors, have_bkcolors;

/* runtime feature usage (unused helpers are compiled out via BC_NO_*) */
static int u_score_add, u_score_sub, u_pfextra, u_rand16;

static Dim *dim_find(const char *n) { Dim *d; for (d = dims; d; d = d->next) if (!strcmp(d->name, n)) return d; return NULL; }
static Con *con_find(const char *n) { Con *c; for (c = consts; c; c = c->next) if (!strcmp(c->name, n)) return c; return NULL; }

/* ensure a const's value is computed */
static long fold_expr(Expr *e, int depth);
static void eval_const(Con *c)
{
    if (!c->done) {
        c->val = fold_expr(c->expr, 0);
        c->done = 1;
    }
}
static DT  *dt_find(const char *n)  { DT *t; for (t = dtables; t; t = t->next) if (!strcmp(t->name, n)) return t; return NULL; }

static Lab *lab_get(const char *n)
{
    Lab *l;
    for (l = labels; l; l = l->next) if (!strcmp(l->name, n)) return l;
    l = calloc(1, sizeof *l);
    l->name = bc_strdup_lower(n);
    l->next = labels;
    labels = l;
    return l;
}

/* names provided by the runtime header -- users may not redefine these */
static const char *reserved[] = {
    "temp1","temp2","temp3","temp4","temp5","temp6","temp7",
    "player0x","player0y","player1x","player1y",
    "playfield","score","scorecolor","rand16",
    /* TIA */
    "vsync","vblank","wsync","rsync","nusiz0","nusiz1","colup0","colup1",
    "colupf","colubk","ctrlpf","refp0","refp1","pf0","pf1","pf2",
    "resp0","resp1","resm0","resm1","resbl","audc0","audc1","audf0",
    "audf1","audv0","audv1","grp0","grp1","enam0","enam1","enabl",
    "hmp0","hmp1","hmm0","hmm1","hmbl","vdelp0","vdelp1","vdelbl",
    "resmp0","resmp1","hmove","hmclr","cxclr",
    "cxm0p","cxm1p","cxp0fb","cxp1fb","cxm0fb","cxm1fb","cxblpf","cxppmm",
    "inpt0","inpt1","inpt2","inpt3","inpt4","inpt5",
    /* RIOT */
    "swcha","swacnt","swchb","swbcnt","intim","timint","tim1t","tim8t","tim64t","t1024t",
    /* helpers */
    "joy0up","joy0down","joy0left","joy0right","joy0fire",
    "joy1up","joy1down","joy1left","joy1right","joy1fire",
    "main","TIA","RIOT", NULL
};
static int is_reserved(const char *n)
{
    int i;
    for (i = 0; reserved[i]; i++) if (!strcmp(reserved[i], n)) return 1;
    return 0;
}
static int is_ckeyword(const char *n)
{
    static const char *kw[] = {
        "auto","break","case","char","const","continue","default","do","double",
        "else","enum","extern","float","for","goto","if","int","long","register",
        "return","short","signed","sizeof","static","struct","switch","typedef",
        "union","unsigned","void","volatile","while", NULL };
    int i;
    for (i = 0; kw[i]; i++) if (!strcmp(kw[i], n)) return 1;
    return 0;
}
static const char *ck_mangle(const char *n)
{
    if (!is_ckeyword(n)) return n;
    {
        static char buf[64][2][128];
        static int k;
        char (*slot)[128] = &buf[k & 63][0];
        snprintf(slot, 128, "%s_", n);
        return *slot;
    }
}

/* register name -> C expression via TIA/RIOT structs */
static int is_riot_reg(const char *n)
{
    static const char *r[] = { "swcha","swacnt","swchb","swbcnt","intim","timint",
        "tim1t","tim8t","tim64t","t1024t", NULL };
    int i;
    for (i = 0; r[i]; i++) if (!strcmp(r[i], n)) return 1;
    return 0;
}

/* resolve an identifier used in a value context to C code */
static void note_var(const char *b);
static int is_tia_reg(const char *n);
static char *resolve_ident(const char *n, int line)
{
    char buf[256];
    if (!strcmp(n, "rand"))    return bc_strdup("bc_rand()");
    if (!strcmp(n, "rand16"))  { u_rand16 = 1; return bc_strdup("bc_rand16()"); }
    if (con_find(n)) {
        Con *c = con_find(n);
        eval_const(c);
        snprintf(buf, sizeof buf, "(%ld)", c->val);
        return bc_strdup(buf);
    }
    {
        Dim *d = dim_find(n);
        if (d) {
            if (d->is_addr) { snprintf(buf, sizeof buf, "(*(unsigned char*)0x%02lX)", d->addr); }
            else            { snprintf(buf, sizeof buf, "%s", ck_mangle(d->base)); note_var(d->base); }
            return bc_strdup(buf);
        }
    }
    if (n[0] && !n[1] && n[0] >= 'a' && n[0] <= 'z') {
        var_used[n[0]-'a'] = 1;
        return bc_strdup(n);          /* letters never collide */
    }
    if (!strncmp(n, "temp", 4) && strlen(n)==5 && n[4]>='1' && n[4]<='7') {
        temp_used[n[4]-'1'] = 1;
        return bc_strdup(n);
    }
    if (!strcmp(n, "playfield"))   return bc_strdup("bc_pf");
    if (!strcmp(n, "score"))       return bc_strdup("bc_score");
    if (!strcmp(n, "scorecolor"))  { have_scorecolor = 1; return bc_strdup("bc_scorecolor"); }
    if (is_tia_reg(n))  { snprintf(buf, sizeof buf, "TIA.%s", n);  return bc_strdup(buf); }
    if (is_riot_reg(n)) { snprintf(buf, sizeof buf, "RIOT.%s", n); return bc_strdup(buf); }
    if (is_reserved(n)) return bc_strdup(n);   /* kernel globals & macros */
    cg_fatal(line, "'%s' is not defined (use dim/const)", n);
    return NULL;
}
static void note_var(const char *b)
{
    if (b[0] && !b[1] && b[0]>='a' && b[0]<='z') { var_used[b[0]-'a'] = 1; return; }
    if (!strncmp(b, "temp", 4) && strlen(b)==5 && b[4]>='1' && b[4]<='7') { temp_used[b[4]-'1'] = 1; return; }
}

static int is_tia_reg(const char *n)
{
    static const char *r[] = {
        "vsync","vblank","wsync","rsync","nusiz0","nusiz1","colup0","colup1",
        "colupf","colubk","ctrlpf","refp0","refp1","pf0","pf1","pf2",
        "resp0","resp1","resm0","resm1","resbl","audc0","audc1","audf0",
        "audf1","audv0","audv1","grp0","grp1","enam0","enam1","enabl",
        "hmp0","hmp1","hmm0","hmm1","hmbl","vdelp0","vdelp1","vdelbl",
        "resmp0","resmp1","hmove","hmclr","cxclr",
        "cxm0p","cxm1p","cxp0fb","cxp1fb","cxm0fb","cxm1fb","cxblpf","cxppmm",
        "inpt0","inpt1","inpt2","inpt3","inpt4","inpt5", NULL };
    int i;
    for (i = 0; r[i]; i++) if (!strcmp(r[i], n)) return 1;
    return 0;
}

/* ---------------- compile-time constant folding ---------------- */

static long fold_expr(Expr *e, int depth)
{
    if (depth > 64) cg_fatal(e->line, "constant expression too deep (cycle?)");
    switch (e->type) {
    case EX_NUM:
        return e->num;
    case EX_ID: {
        Con *c = con_find(e->id);
        if (!c) cg_fatal(e->line, "'%s' is not a constant", e->id);
        if (c->done) return c->val;
        c->done = 1;                       /* cycle guard */
        c->val = fold_expr(c->expr, depth + 1);
        return c->val;
    }
    case EX_UN:
        if (e->op == '-') return -fold_expr(e->a, depth + 1);
        break;
    case EX_BIN: {
        long a = fold_expr(e->a, depth + 1), b = fold_expr(e->b, depth + 1);
        switch (e->op) {
        case '+': return a + b;
        case '-': return a - b;
        case '*': return a * b;
        case '/': if (!b) cg_fatal(e->line, "divide by zero"); return a / b;
        case MOD: if (!b) cg_fatal(e->line, "divide by zero"); return a % b;
        case '&': return a & b;
        case '|': return a | b;
        case '^': return a ^ b;
        case SHL: return a << b;
        case SHR: return a >> b;
        }
        break;
    }
    }
    cg_fatal(e->line, "initializer is not a compile-time constant");
    return 0;
}

static void add_const(char *name, Expr *expr, int line)
{
    Con *c;
    if (con_find(name) || dim_find(name) || dt_find(name))
        cg_fatal(line, "'%s' is already defined", name);
    if (is_reserved(name))
        cg_fatal(line, "'%s' is a reserved name", name);
    c = calloc(1, sizeof *c);
    c->name = name; c->expr = expr;
    c->val = fold_expr(expr, 0);
    c->done = 1;
    c->next = consts; consts = c;
}

static void add_dim(char *name, char *base, long addr, int is_addr, int line)
{
    Dim *d;
    if (con_find(name) || dim_find(name) || dt_find(name))
        cg_fatal(line, "'%s' is already defined", name);
    if (is_reserved(name))
        cg_fatal(line, "'%s' collides with a system variable", name);
    if (!is_addr && !dim_find(base)) {
        /* base must be a letter var or tempN */
        if (!(base[0] && !base[1] && base[0] >= 'a' && base[0] <= 'z') &&
            !(strlen(base) == 5 && !strncmp(base, "temp", 4) && base[4] >= '1' && base[4] <= '7') &&
            strcmp(base, "score") != 0)
            cg_fatal(line, "dim '%s' refers to unknown variable '%s'", name, base);
    }
    d = calloc(1, sizeof *d);
    d->name = name; d->base = base ? bc_strdup_lower(base) : NULL;
    d->addr = addr; d->is_addr = is_addr;
    d->next = dims; dims = d;
}

/* ---------------- byte-list / block payload parsing ---------------- */

static int parse_byte(const char *tok, int line)
{
    long v;
    if (tok[0] == '$' && tok[1]) v = strtol(tok + 1, NULL, 16);
    else if (tok[0] == '%' && tok[1]) v = strtol(tok + 1, NULL, 2);
    else if ((tok[0] >= '0' && tok[0] <= '9')) v = strtol(tok, NULL, 10);
    else cg_fatal(line, "bad data value '%s'", tok);
    if (v < 0 || v > 255) cg_warn(line, "data value %ld out of range, truncating", v);
    return (int)(v & 255);
}
static int split_bytes(char *payload, unsigned char *out, int maxout, int line)
{
    static const char sep[] = " \t\r\n,";
    char *p = strtok(payload, sep);
    int n = 0;
    while (p) {
        if (n < maxout) out[n++] = (unsigned char)parse_byte(p, line);
        p = strtok(NULL, sep);
    }
    return n;
}

static void add_datatable(char *name, char *payload, int is_sdata, long start, int line)
{
    DT *t;
    static char paybuf[16384];
    unsigned char tmp[256];
    int n;
    if (con_find(name) || dim_find(name) || dt_find(name) || is_reserved(name))
        cg_fatal(line, "data table '%s' redefines something", name);
    snprintf(paybuf, sizeof paybuf, "%s", payload);
    n = split_bytes(paybuf, tmp, 256, line);
    if (n >= 256) cg_fatal(line, "data table '%s' exceeds 255 bytes", name);
    if (n == 0) cg_warn(line, "data table '%s' is empty", name);
    t = calloc(1, sizeof *t);
    t->name = bc_strdup_lower(name);
    memcpy(t->bytes, tmp, sizeof tmp);
    t->len = n ? n : 1;
    t->start = start & 255;
    t->next = dtables;
    dtables = t;
}

static void add_player(int which, char *payload, int line)
{
    static char paybuf[4096];
    unsigned char bytes[256];
    int n, i;
    char *dup = bc_strdup(payload);
    snprintf(paybuf, sizeof paybuf, "%s", payload);
    n = split_bytes(paybuf, bytes, 256, line);
    free(dup);
    if (n == 0) { n = 8; memset(bytes, 0, 8); }
    if (n > 8) { cg_warn(line, "player%d has %d rows; kernel draws first 8", which, n); n = 8; }
    for (i = n; i < 8; i++) bytes[i] = 0;
    oprintf(&g_pro, "static const unsigned char bc_rom_p%d[8] = {", which);
    for (i = 0; i < 8; i++) oprintf(&g_pro, "%s0x%02X", i ? "," : "", bytes[i]);
    oprintf(&g_pro, "};\n");
    olinef(&g_init, 1, "memcpy(bc_p%dgfx, bc_rom_p%d, 8);", which, which);
    oput(&g_init, "\n");
}

#define PF_ROWS 11
#define PF_COLS 32
static void pf_setcol(unsigned char *rowbytes, int col, int bit)
{
    if (col < 4)       rowbytes[0] |= (unsigned char)(0x80u >> col);
    else if (col < 12) rowbytes[1] |= (unsigned char)(0x80u >> (col - 4));
    else if (col < 20) rowbytes[2] |= (unsigned char)(0x80u >> (col - 12));
    else rowbytes[3] |= (unsigned char)(0x80u >> (31 - col));
}
static void add_playfield(char *payload, int line)
{
    char *rows[PF_ROWS];
    int nrows = 0, r, c;
    char *p = payload;
    memset(rows, 0, sizeof rows);
    while (*p && nrows < PF_ROWS) {
        char *eol = strchr(p, '\n');
        size_t len;
        if (!eol) eol = p + strlen(p);
        len = (size_t)(eol - p);
        if (len) {
            rows[nrows] = malloc(len + 1);
            memcpy(rows[nrows], p, len); rows[nrows][len] = 0;
            nrows++;
        }
        p = *eol ? eol + 1 : eol;
    }
    if (nrows == 0) cg_warn(line, "empty playfield");
    oprintf(&g_pro, "static const unsigned char bc_rom_pf[%d] = {\n", PF_ROWS * 4);
    for (r = 0; r < PF_ROWS; r++) {
        unsigned char rb[4] = {0, 0, 0, 0};
        const char *row = rows[r] ? rows[r] : "";
        for (c = 0; c < PF_COLS; c++) {
            char ch = row[c];
            int on;
            if (!ch) ch = '.';
            if (ch == 'x' || ch == 'X' || ch == '1' || ch == '#') on = 1;
            else if (ch == '.' || ch == '0' || ch == '_') on = 0;
            else { cg_warn(line, "bad playfield character '%c', treating as empty", ch); on = 0; }
            if (on) {
                int cc = c;
                if (cc >= 20) cc = 39 - cc;   /* mirror right half onto left registers */
                pf_setcol(rb, cc, on);
            }
        }
        oprintf(&g_pro, "    ");
        for (c = 0; c < 4; c++) {
            int idx = r*4 + c;
            oprintf(&g_pro, "0x%02X%s", rb[c], (idx < PF_ROWS*4-1) ? "," : "");
        }
        oprintf(&g_pro, "\n");
    }
    oprintf(&g_pro, "};\n");
    olinef(&g_init, 1, "memcpy(bc_pf, bc_rom_pf, %d);", PF_ROWS*4);
    oput(&g_init, "\n");
}

static void add_colors(int which, char *payload, int line)
{
    static char paybuf[2048];
    unsigned char vals[16];
    int n, i;
    snprintf(paybuf, sizeof paybuf, "%s", payload);
    n = split_bytes(paybuf, vals, 11, line);
    for (i = n; i < 11; i++) vals[i] = vals[n ? n-1 : 0];
    oprintf(&g_pro, "static const unsigned char bc_rom_%s[%d] = {",
            which ? "bk" : "pfc", PF_ROWS);
    for (i = 0; i < PF_ROWS; i++) oprintf(&g_pro, "%s0x%02X", i ? "," : "", vals[i]);
    oprintf(&g_pro, "};\n");
    olinef(&g_init, 1, "%s = bc_rom_%s;",
           which ? "bc_bkcolors" : "bc_pfcolors", which ? "bk" : "pfc");
    if (which) have_bkcolors = 1; else have_pfcolors = 1;
}

/* ---------------- expression / condition text ---------------- */

static char *ex_str(Expr *e);
static char *cd_str(Cond *c);

static const char *binop_text(int op)
{
    switch (op) {
    case '+': return "+";
    case '-': return "-";
    case '*': return "*";
    case '/': return "/";
    case MOD: return "%";
    case '&': return "&";
    case '|': return "|";
    case '^': return "^";
    case SHL: return "<<";
    case SHR: return ">>";
    case EQ: return "==";
    case NE: return "!=";
    case LT: return "<";
    case GT: return ">";
    case LE: return "<=";
    case GE: return ">=";
    case ANDAND: return "&&";
    case OROR: return "||";
    }
    return "?";
}

static const char *cmpop_text(int op)
{
    switch (op) {
    case EQ: return "==";
    case NE: return "!=";
    case LT: return "<";
    case GT: return ">";
    case LE: return "<=";
    case GE: return ">=";
    }
    return "?";
}

/* collision(a,b) support: TIA collision register/mask per pair */
static int coll_pair(const char *a, const char *b, const char **reg, unsigned *mask)
{
    static const struct { const char *a, *b, *reg; unsigned mask; } tbl[] = {
        {"player0","player1","cxppmm",0x80},
        {"missile0","missile1","cxppmm",0x40},
        {"player0","missile0","cxm0p",0x80},
        {"player0","missile1","cxm0p",0x40},
        {"player1","missile0","cxm1p",0x80},
        {"player1","missile1","cxm1p",0x40},
        {"player0","playfield","cxp0fb",0x80},
        {"player1","playfield","cxp1fb",0x80},
        {"player0","ball","cxp0fb",0x80},      /* shares P0-vs-(pf|ball) reg */
        {"player1","ball","cxp1fb",0x80},
        {"missile0","playfield","cxm0fb",0x80},
        {"missile1","playfield","cxm1fb",0x80},
        {"missile0","ball","cxm0fb",0x80},     /* shares M0-vs-(pf|ball) reg */
        {"missile1","ball","cxm1fb",0x80},
        {"ball","playfield","cxblpf",0x80},
        {NULL,NULL,NULL,0}
    };
    int i;
    for (i = 0; tbl[i].a; i++)
        if ((!strcmp(tbl[i].a,a) && !strcmp(tbl[i].b,b)) ||
            (!strcmp(tbl[i].a,b) && !strcmp(tbl[i].b,a))) {
            *reg = tbl[i].reg; *mask = tbl[i].mask;
            return 1;
        }
    return 0;
}

static char *ex_str(Expr *e)
{
    char buf[1024];
    switch (e->type) {
    case EX_NUM:
        snprintf(buf, sizeof buf, "%ld", e->num);
        return bc_strdup(buf);
    case EX_ID:
        return resolve_ident(e->id, e->line);
    case EX_UN: {
        char *a = ex_str(e->a);
        snprintf(buf, sizeof buf, "(unsigned char)(-(%s))", a);
        free(a);
        return bc_strdup(buf);
    }
    case EX_BIN: {
        char *a = ex_str(e->a), *b = ex_str(e->b);
        snprintf(buf, sizeof buf, "(unsigned char)((%s) %s (%s))",
                 a, binop_text(e->op), b);
        free(a); free(b);
        return bc_strdup(buf);
    }
    case EX_BITREAD: {
        char *v = ex_str(e->a), *bt = ex_str(e->b);
        snprintf(buf, sizeof buf, "(((%s) >> (%s)) & 1)", v, bt);
        free(v); free(bt);
        return bc_strdup(buf);
    }
    case EX_INDEX: {
        DT *t = dt_find(e->id);
        if (t) {
            char *ix = ex_str(e->a);
            snprintf(buf, sizeof buf, "(bc_dt_%s[(unsigned char)(%s)])", t->name, ix);
            free(ix);
            return bc_strdup(buf);
        }
        {
            Dim *d = dim_find(e->id);
            char *ix;
            if (!d) cg_fatal(e->line, "'%s' is not an array (dim it first)", e->id);
            d->indexed = 1;
            ix = ex_str(e->a);
            if (d->is_addr)
                snprintf(buf, sizeof buf, "(((unsigned char*)0x%02lX)[(unsigned char)(%s)])", d->addr, ix);
            else {
                char *bs = resolve_ident(d->base, e->line);
                snprintf(buf, sizeof buf, "((%s)[(unsigned char)(%s)])", bs, ix);
                free(bs);
            }
            free(ix);
            return bc_strdup(buf);
        }
    }
    case EX_CALL: {
        ExprList *al = e->args;
        int nargs = 0;
        Expr *A = NULL, *B = NULL;
        while (al) { if (nargs==0) A = al->e; else if (nargs==1) B = al->e; nargs++; al = al->next; }
        if (!strcmp(e->id, "sread")) {
            DT *t;
            if (nargs != 1 || A->type != EX_ID)
                cg_fatal(e->line, "sread() takes a data table name");
            t = dt_find(A->id);
            if (!t) cg_fatal(e->line, "sread: unknown data table '%s'", A->id);
            t->sread_used = 1;
            snprintf(buf, sizeof buf, "(bc_dt_%s[bc_sri_%s++])", t->name, t->name);
            return bc_strdup(buf);
        }
        if (!strcmp(e->id, "collision")) {
            const char *reg; unsigned mask;
            if (nargs != 2 || !A || !B || A->type != EX_ID || B->type != EX_ID ||
                !coll_pair(A->id, B->id, &reg, &mask))
                cg_fatal(e->line, "unsupported collision pair "
                         "(use player0/1, missile0/1, ball, playfield)");
            snprintf(buf, sizeof buf, "(TIA.%s & 0x%02X)", reg, mask);
            return bc_strdup(buf);
        }
        if (!strcmp(e->id, "pfread")) {
            u_pfextra = 1;
            char *x, *y;
            if (nargs != 2 || !A || !B) cg_fatal(e->line, "pfread(x,y) takes 2 args");
            x = ex_str(A); y = ex_str(B);
            snprintf(buf, sizeof buf, "bc_pfread((unsigned char)(%s),(unsigned char)(%s))", x, y);
            free(x); free(y);
            return bc_strdup(buf);
        }
        cg_fatal(e->line, "unknown function '%s'", e->id);
        return NULL;
    }
    }
    cg_fatal(e->line, "internal: bad expression");
    return NULL;
}

static char *cd_str(Cond *c)
{
    char buf[2048];
    switch (c->type) {
    case CD_VAL: {
        char *e = ex_str(c->e);
        snprintf(buf, sizeof buf, "(%s)", e);
        free(e);
        break;
    }
    case CD_CMP: {
        char *a = ex_str(c->e), *b = ex_str(c->eright);
        snprintf(buf, sizeof buf, "((%s) %s (%s))", a, cmpop_text(c->cmpop), b);
        free(a); free(b);
        break;
    }
    case CD_AND: case CD_OR: {
        char *a = cd_str(c->a), *b = cd_str(c->b);
        snprintf(buf, sizeof buf, "((%s) %s (%s))", a, c->type == CD_AND ? "&&" : "||", b);
        free(a); free(b);
        break;
    }
    case CD_NOT: {
        char *a = cd_str(c->a);
        snprintf(buf, sizeof buf, "(!( %s ))", a);
        free(a);
        break;
    }
    default: strcpy(buf, "1");
    }
    return bc_strdup(buf);
}

static const char *lab_cname(const char *n)
{
    static char bufs[8][128];
    static int k;
    char *b = bufs[k++ & 7];
    snprintf(b, 128, "u_%s", n);
    return b;
}

static int next_site(void) { return n_gosubs++; }

/* ---------------- statement emission ---------------- */

static void emit_list(Out *o, StmtList *l, int indent);

static int is_score(Expr *e) { return e && e->type == EX_ID && !strcmp(e->id, "score"); }

static void emit_assign(Out *o, Stmt *s, int indent)
{
    Expr *t = s->e1, *rhs = s->e2;
    if (!t || !rhs) cg_fatal(s->line, "bad assignment");
    if (t->type == EX_ID) {
        char *L;
        if (dt_find(t->id)) cg_fatal(s->line, "data tables are read-only");
        if (!strcmp(t->id, "score")) {
            if (rhs->type == EX_BIN && (rhs->op == '+' || rhs->op == '-') && is_score(rhs->a)) {
                if (rhs->op == '+') u_score_add = 1; else u_score_sub = 1;
                olinef(o, indent, "bc_score_%s((unsigned char)(%s));",
                       rhs->op == '+' ? "add" : "sub", ex_str(rhs->b));
            }
            else
                olinef(o, indent, "bc_score_set((unsigned char)(%s));", ex_str(rhs));
            return;
        }
        L = resolve_ident(t->id, s->line);
        olinef(o, indent, "%s = (unsigned char)(%s);", L, ex_str(rhs));
        return;
    }
    if (t->type == EX_BITREAD) {
        Expr *v = t->a;
        char *L;
        if (v->type != EX_ID) cg_fatal(s->line, "bit writes need a plain variable");
        L = resolve_ident(v->id, s->line);
        if (t->b->type == EX_NUM) {
            long b = t->b->num;
            if (b < 0 || b > 7) cg_fatal(s->line, "bit index %ld out of 0..7", b);
            olinef(o, indent,
                   "%s = ((%s) & ~(unsigned char)(1 << %ld)) | ((((unsigned char)(%s)) & 1) << %ld);",
                   L, L, b, ex_str(rhs), b);
        } else {
            olinef(o, indent, "{ unsigned char bc_bt = (unsigned char)(%s);", ex_str(t->b));
            olinef(o, indent + 1,
                   "%s = ((%s) & ~(unsigned char)(1 << bc_bt)) | ((((unsigned char)(%s)) & 1) << bc_bt);",
                   L, L, ex_str(rhs));
            olinef(o, indent, "}");
        }
        return;
    }
    if (t->type == EX_INDEX) {
        DT *tt = dt_find(t->id);
        Dim *d;
        char *ix, *rs;
        if (tt) cg_fatal(s->line, "data tables are read-only");
        d = dim_find(t->id);
        if (!d) cg_fatal(s->line, "'%s' is not an array", t->id);
        d->indexed = 1;
        ix = ex_str(t->a); rs = ex_str(rhs);
        if (d->is_addr)
            olinef(o, indent, "((unsigned char*)0x%02lX)[(unsigned char)(%s)] = (unsigned char)(%s);",
                   d->addr, ix, rs);
        else {
            char *bs = resolve_ident(d->base, s->line);
            olinef(o, indent, "(%s)[(unsigned char)(%s)] = (unsigned char)(%s);", bs, ix, rs);
            free(bs);
        }
        free(ix); free(rs);
        return;
    }
    cg_fatal(s->line, "cannot assign to this expression");
}

static void push_loop(int type, char *var)
{
    LoopCtx *c = calloc(1, sizeof *c);
    c->type = type; c->uid = uid_counter++; c->var = var ? bc_strdup(var) : NULL;
    c->up = loops; loops = c;
}
static LoopCtx *pop_loop(int type, const char *what, int line)
{
    LoopCtx *c = loops;
    if (!c || c->type != type)
        cg_fatal(line, "'%s' without matching open block", what);
    loops = c->up;
    return c;
}

static void emit_stmt(Out *o, Stmt *s, int indent);

static void emit_list(Out *o, StmtList *l, int indent)
{
    for (; l; l = l->next) emit_stmt(o, l->s, indent);
}

static void emit_stmt(Out *o, Stmt *s, int indent)
{
    switch (s->type) {
    case S_LABEL: {
        Lab *l = lab_get(s->s1);
        if (l->defined++) cg_fatal(s->line, "duplicate label '%s'", s->s1);
        olinef(o, indent, "%s: ;", lab_cname(s->s1));
        break;
    }
    case S_ASSIGN:
        emit_assign(o, s, indent);
        break;

    case S_IF: {
        char *cs = cd_str(s->cond);
        olinef(o, indent, "if (%s)", cs);
        free(cs);
        olinef(o, indent, "{");
        emit_list(o, s->body, indent + 1);
        if (s->elsebody) {
            olinef(o, indent, "} else {");
            emit_list(o, s->elsebody, indent + 1);
        }
        olinef(o, indent, "}");
        break;
    }

    case S_WHILE: {
        char *cs = cd_str(s->cond);
        push_loop(LP_WHILE, NULL);
        olinef(o, indent, "while (%s)", cs);
        free(cs);
        olinef(o, indent, "{");
        break;
    }
    case S_WEND:
        pop_loop(LP_WHILE, "wend", s->line);
        olinef(o, indent, "}");
        break;

    case S_DO:
        push_loop(LP_DO, NULL);
        olinef(o, indent, "do");
        olinef(o, indent, "{");
        break;
    case S_LOOP: {
        pop_loop(LP_DO, "loop", s->line);
        if (s->cond) {
            char *cs = cd_str(s->cond);
            if (s->n1) olinef(o, indent, "} while (!(%s));", cs);   /* until */
            else       olinef(o, indent, "} while (%s);", cs);      /* while */
            free(cs);
        } else {
            olinef(o, indent, "} while (1);");
        }
        break;
    }

    case S_FOR: {
        char *V = resolve_ident(s->s1, s->line);
        char *st = s->e3 ? ex_str(s->e3) : bc_strdup("1");
        push_loop(LP_FOR, s->s1);
        olinef(o, indent, "{ unsigned char bc_fe%d = (unsigned char)(%s);", loops->uid, ex_str(s->e2));
        olinef(o, indent, "  signed char bc_fs%d = (signed char)(unsigned char)(%s);", loops->uid, st);
        olinef(o, indent, "  %s = (unsigned char)(%s);", V, ex_str(s->e1));
        olinef(o, indent, "  for (;;)");
        olinef(o, indent, "  {");
        break;
    }
    case S_NEXT: {
        LoopCtx *c = loops;
        if (!c || c->type != LP_FOR) cg_fatal(s->line, "next without for");
        if (s->s1 && strcmp(c->var, s->s1))
            cg_fatal(s->line, "next %s does not match for %s", s->s1, c->var);
        loops = c->up;
        {
            char *V = resolve_ident(c->var, s->line);
            olinef(o, indent, "  %s = (unsigned char)((%s) + bc_fs%d);", V, V, c->uid);
            olinef(o, indent, "    if ((signed char)bc_fs%d >= 0) { if ((%s) > bc_fe%d) break; }",
                   c->uid, V, c->uid);
            olinef(o, indent, "    else { if ((%s) < bc_fe%d) break; }", V, c->uid);
        }
        olinef(o, indent, "  }");
        olinef(o, indent, "}");
        break;
    }

    case S_EXIT:
        if (!loops) cg_fatal(s->line, "exit used outside a loop");
        olinef(o, indent, "break;");
        break;

    case S_GOTO: {
        Lab *l = lab_get(s->s1);
        l->refs++;
        olinef(o, indent, "goto %s;", lab_cname(s->s1));
        break;
    }
    case S_GOSUB: {
        Lab *l = lab_get(s->s1);
        int id = next_site();
        l->refs++;
        olinef(o, indent, "if (bc_gsp >= BC_GOSUB_DEPTH) { bc_gsp = 0; goto bc_start; }");
        olinef(o, indent, "bc_gs[bc_gsp++] = %d;", id);
        olinef(o, indent, "goto %s;", lab_cname(s->s1));
        olinef(o, indent, "bc_ret%d: ;", id);
        break;
    }
    case S_RETURN:
        olinef(o, indent, "if (!bc_gsp) goto bc_start;");
        olinef(o, indent, "bc_disp = bc_gs[--bc_gsp];");
        olinef(o, indent, "goto bc_dispatch;");
        break;
    case S_POP:
        olinef(o, indent, "if (bc_gsp) bc_gsp--;");
        break;
    case S_DRAWSCREEN:
        olinef(o, indent, "bc_drawscreen();");
        break;
    case S_REBOOT:
        olinef(o, indent, "__asm__(\"\\tjmp\\t($FFFC)\");");
        break;

    case S_ON: {
        char *es = ex_str(s->e1);
        char *names = bc_strdup(s->s3 ? s->s3 : "");
        char *p = names;
        int idx = 0;
        olinef(o, indent, "switch ((unsigned char)(%s))", es);
        olinef(o, indent, "{");
        while (*p) {
            char *q = strchr(p, ' ');
            if (q) *q = 0;
            if (*p) {
                Lab *l = lab_get(p);
                l->refs++;
                if (s->n2) {   /* gosub */
                    int id = next_site();
                    olinef(o, indent + 1, "case %d:", idx + 1);
                    olinef(o, indent + 2, "if (bc_gsp >= BC_GOSUB_DEPTH) { bc_gsp = 0; goto bc_start; }");
                    olinef(o, indent + 2, "bc_gs[bc_gsp++] = %d;", id);
                    olinef(o, indent + 2, "goto %s;", lab_cname(l->name));
                    olinef(o, indent + 2, "bc_ret%d: ;", id);
                } else {
                    olinef(o, indent + 1, "case %d: goto %s;", idx + 1, lab_cname(l->name));
                }
                idx++;
            }
            p += strlen(p) + (q ? 1 : 0);
        }
        free(names);
        olinef(o, indent + 1, "default: break;");
        olinef(o, indent, "}");
        break;
    }

    case S_DIM:
        add_dim(s->s1, s->s2, s->n1, (int)s->n2, s->line);
        break;
    case S_CONST:
        add_const(s->s1, s->e1, s->line);
        break;
    case S_SET: {
        char *args = bc_strdup(s->s3 ? s->s3 : "");
        char *p = args;
        while (*p) {
            char *q = strchr(p, ' ');
            if (q) *q = 0;
            if (!strcmp(p, "tv")) {
                p += strlen(p) + 1;
                if (!strcmp(p, "pal") || !strcmp(p, "secam")) tv_pal = 1;
                else if (strcmp(p, "ntsc")) cg_fatal(s->line, "set tv: unknown mode '%s'", p);
                p += strlen(p);
            } else if (!strcmp(p, "romsize")) {
                p += strlen(p) + 1;
                if (strcmp(p, "4k")) cg_warn(s->line, "romsize %s unsupported by stock cc65 atari2600.cfg (4k assumed); banking not implemented", p);
                p += strlen(p);
            } else if (!strcmp(p, "smartbranching") || !strcmp(p, "optimization") ||
                       !strcmp(p, "kernel") || !strcmp(p, "legacy")) {
                p += strlen(p) + 1;      /* skip value */
            } else {
                cg_warn(s->line, "ignoring set option '%s'", p);
                p += strlen(p);
            }
            while (*p == ' ') p++;
        }
        free(args);
        break;
    }

    case S_DATA:   add_datatable(s->s1, s->s3, 0, 0, s->line); break;
    case S_SDATA:  add_datatable(s->s1, s->s3, 1, s->n1, s->line); break;
    case S_PLAYER: add_player((int)s->n1, s->s3, s->line); break;
    case S_PF:     add_playfield(s->s3, s->line); break;
    case S_PFCOL:  add_colors(0, s->s3, s->line); break;
    case S_BKCOL:  add_colors(1, s->s3, s->line); break;
    case S_LIVES:  cg_fatal(s->line, "lives: blocks are not supported yet"); break;

    case S_ASM: {
        char *dup = bc_strdup(s->s3 ? s->s3 : "");
        char *p = dup, *nl;
        while (*p) {
            nl = strchr(p, '\n');
            if (nl) *nl = 0;
            if (*p && strspn(p, " \t\r") != strlen(p)) {
                char esc[2048];
                size_t i, j = 0;
                for (i = 0; p[i] && j < sizeof esc - 8; i++) {
                    if (p[i] == '"' || p[i] == '\\') esc[j++] = '\\';
                    esc[j++] = p[i];
                }
                esc[j] = 0;
                olinef(o, indent, "__asm__(\"%s\");", esc);
            }
            p = nl ? nl + 1 : p + strlen(p);
        }
        free(dup);
        break;
    }

    case S_PFPX:
        olinef(o, indent, "bc_pfpixel((unsigned char)(%s), (unsigned char)(%s), %ld);",
               ex_str(s->e1), ex_str(s->e2), s->n1);
        break;
    case S_PFHLINE:
        u_pfextra = 1;
        olinef(o, indent, "bc_pfhline((unsigned char)(%s), (unsigned char)(%s), (unsigned char)(%s), %ld);",
               ex_str(s->e1), ex_str(s->e2), ex_str(s->e3), s->n1);
        break;
    case S_PFVLINE:
        u_pfextra = 1;
        olinef(o, indent, "bc_pfvline((unsigned char)(%s), (unsigned char)(%s), (unsigned char)(%s), %ld);",
               ex_str(s->e1), ex_str(s->e2), ex_str(s->e3), s->n1);
        break;
    case S_PFSCROLL:
        u_pfextra = 1;
        olinef(o, indent, "bc_pfscroll(%ld);", s->n1);
        break;
    case S_PFCLEAR:
        u_pfextra = 1;
        olinef(o, indent, "bc_pfclear((unsigned char)(%s));",
               s->e1 ? ex_str(s->e1) : "0");
        break;

    default:
        cg_fatal(s->line, "internal: unhandled statement type %d", s->type);
    }
}

/* ---------------- driver ---------------- */

void cg_init(void)
{
    memset(&g_pro, 0, sizeof g_pro);
    memset(&g_init, 0, sizeof g_init);
    memset(&g_body, 0, sizeof g_body);
}

int cg_run(StmtList *prog, const char *srcname, const char *outpath)
{
    int i;
    Lab *l;
    FILE *f;

    g_src = srcname;
    f = fopen(outpath, "w");
    if (!f) {
        fprintf(stderr, "bC: cannot open '%s' for writing\n", outpath);
        return 1;
    }

    emit_list(&g_body, prog, 1);

    fprintf(f, "/* Generated by bC -- batari Basic to cc65 C transpiler */\n");
    fprintf(f, "/* Source: %s */\n\n", srcname);
    if (tv_pal) fprintf(f, "#define BC_PAL 1\n");
    if (!u_score_add) fprintf(f, "#define BC_NO_SCORE_ADD\n");
    if (!u_score_sub) fprintf(f, "#define BC_NO_SCORE_SUB\n");
    if (!u_pfextra)   fprintf(f, "#define BC_NO_PFEXTRA\n");
    if (!u_rand16)    fprintf(f, "#define BC_NO_RAND16\n");
    fprintf(f, "#include <atari2600.h>\n");
    fprintf(f, "#include \"bc_runtime.h\"\n");
    fprintf(f, "#include \"bc_kernel.c\"\n\n");

    fputs(g_pro.s ? g_pro.s : "", f);
    for (i = 0; i < 26; i++)
        if (var_used[i]) fprintf(f, "unsigned char %c;\n", 'a' + i);
    for (i = 0; i < 7; i++)
        if (temp_used[i]) fprintf(f, "unsigned char temp%d;\n", i + 1);
    {
        DT *t;
        for (t = dtables; t; t = t->next) {
            int k;
            fprintf(f, "static const unsigned char bc_dt_%s[%d] = {", t->name, t->len);
            for (k = 0; k < t->len; k++)
                fprintf(f, "%s0x%02X", k ? "," : "", t->bytes[k]);
            fprintf(f, "};\n");
            if (t->sread_used)
                fprintf(f, "static unsigned char bc_sri_%s = %ld;\n", t->name, t->start);
        }
    }
    fputc('\n', f);

    fprintf(f, "int main(void)\n{\n");
    if (n_gosubs > 0) {
        int k;
        fprintf(f, "    unsigned char bc_gs[BC_GOSUB_DEPTH];\n");
        fprintf(f, "    unsigned char bc_gsp = 0;\n");
        fprintf(f, "    unsigned char bc_disp = 0;\n\n");
        fprintf(f, "    bc_init();\n\n");
        fputs(g_init.s ? g_init.s : "", f);
        fprintf(f, "    goto bc_start;\n");
        fprintf(f, "bc_dispatch:\n");
        fprintf(f, "    switch (bc_disp) {\n");
        for (k = 0; k < n_gosubs; k++)
            fprintf(f, "    case %d: goto bc_ret%d;\n", k, k);
        fprintf(f, "    default: goto bc_start;\n");
        fprintf(f, "    }\n");
    } else {
        fprintf(f, "    bc_init();\n\n");
        fputs(g_init.s ? g_init.s : "", f);
    }
    fprintf(f, "bc_start:\n    ;\n");
    fputs(g_body.s ? g_body.s : "", f);
    fprintf(f, "    ;\n    goto bc_start;\n}\n");
    fclose(f);

    for (l = labels; l; l = l->next) {
        if (l->refs && !l->defined)
            cg_fatal(0, "goto/gosub to undefined label '%s'", l->name);
    }
    return 0;
}

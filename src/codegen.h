/* bC -- batari Basic to C transpiler. Shared declarations. */
#ifndef BCGEN_H
#define BCGEN_H

#include <stddef.h>

/* ---------- expression / condition / statement AST ---------- */

enum {
    EX_NUM, EX_ID, EX_BIN, EX_UN, EX_CALL, EX_INDEX, EX_BITREAD
};

typedef struct Expr Expr;
typedef struct ExprList ExprList;
struct Expr {
    int type;
    long num;              /* EX_NUM */
    char *id;              /* EX_ID, EX_CALL callee, EX_INDEX base */
    int op;                /* EX_BIN / EX_UN operator char or token */
    Expr *a, *b;           /* operands */
    ExprList *args;        /* EX_CALL */
    int line;
};
struct ExprList { Expr *e; ExprList *next; };

enum { CD_AND, CD_OR, CD_NOT, CD_CMP, CD_VAL };
typedef struct Cond Cond;
struct Cond {
    int type;
    int cmpop;             /* CD_CMP: '=', '<', '>' etc */
    Expr *e;               /* CD_VAL / CD_CMP left when reused */
    Expr *eright;
    Cond *a, *b;
};

enum {
    S_ASSIGN, S_IF, S_GOTO, S_GOSUB, S_RETURN, S_POP,
    S_FOR, S_NEXT, S_WHILE, S_WEND, S_DO, S_LOOP, S_EXIT,
    S_DRAWSCREEN, S_REBOOT, S_ON, S_LABEL,
    S_DIM, S_CONST, S_SET,
    S_DATA, S_SDATA, S_ASM, S_PLAYER, S_PF, S_PFCOL, S_BKCOL,
    S_PFPX, S_PFHLINE, S_PFVLINE, S_PFSCROLL, S_PFCLEAR
};

typedef struct Stmt Stmt;
typedef struct StmtList StmtList;
struct Stmt {
    int type, line;
    char *s1, *s2, *s3;        /* identifiers / raw payload */
    Expr *e1, *e2, *e3;
    Cond *cond;
    StmtList *body, *elsebody;
    long n1, n2;               /* modes, addresses, flags */
};
struct StmtList { Stmt *s; StmtList *next; };

/* constructors (bc.y helpers) */
Expr *ex_num(long v, int line);
Expr *ex_id(char *id, int line);
Expr *ex_bin(int op, Expr *a, Expr *b, int line);
Expr *ex_un(int op, Expr *a, int line);
Expr *ex_call(char *id, ExprList *args, int line);
Expr *ex_index(char *id, Expr *i, int line);
Expr *ex_bitread(Expr *v, Expr *bit, int line);
ExprList *exl_one(Expr *e);
ExprList *exl_more(ExprList *l, Expr *e);

Cond *cd_val(Expr *e);
Cond *cd_cmp(int op, Expr *a, Expr *b, int line);
Cond *cd_log(int type, Cond *a, Cond *b);
Cond *cd_not(Cond *a);

Stmt *st_new(int type, int line);
StmtList *stl_one(Stmt *s);
StmtList *stl_more(StmtList *l, Stmt *s);

/* ---------- driver ---------- */
void cg_init(void);
int  cg_run(StmtList *prog, const char *srcname, const char *outpath);

/* error reporting (longjmp-free: prints and exits) */
void cg_fatal(int line, const char *fmt, ...);
void cg_warn(int line, const char *fmt, ...);

/* strdup helpers */
char *bc_strdup(const char *s);
char *bc_strdup_lower(const char *s);

#endif

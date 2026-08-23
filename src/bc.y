/* bC -- batari Basic to cc65-C transpiler. Grammar (bison). */
%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "codegen.h"
#include "bc.tab.h"

extern int bcline;
int yylex(void);
int yyerror(const char *s);

#define LN bcline

static char *cat2(const char *a, const char *b)
{
    /* join with a single space: "a b"; NULL-safe */
    size_t la = a ? strlen(a) : 0, lb = strlen(b);
    char *r = malloc(la + lb + 2);
    if (la) { memcpy(r, a, la); r[la++] = ' '; }
    memcpy(r + la, b, lb + 1);
    return r;
}

static Stmt *mk_goto(char *label)
{
    Stmt *s = st_new(S_GOTO, LN);
    s->s1 = label;
    return s;
}
static Stmt *mk_gosub(char *label)
{
    Stmt *s = st_new(S_GOSUB, LN);
    s->s1 = label;
    return s;
}

StmtList *bc_program = NULL;

static StmtList *stl_join(StmtList *a, StmtList *b)
{
    StmtList *p = a;
    if (!p) return b;
    while (p->next) p = p->next;
    p->next = b;
    return a;
}
%}

%union {
    long num;
    char *id;
    char *str;
    Expr *ex;
    ExprList *el;
    Cond *cd;
    Stmt *st;
    StmtList *sl;
}

%token NEWLINE
%token IF THEN ELSE GOTO GOSUB RETURNKW POP ON FOR TO STEP NEXT WHILE WEND DOKW LOOP EXIT UNTIL
%token DRAWSCREEN REBOOT LET DIM CONST SET DATA SDATA ASMKW
%token PFPX PFHL PFVL PFSCROLL PFCLEAR OFF FLIP UP DOWN LEFT RIGHT
%token SHL SHR LE GE NE EQ ANDAND OROR MOD
%token LT GT
%token <num> NUMBER PLAYERBLK PLAYFIELDBLK PFCOLORSBLK BKCOLORSBLK
%token <id>  IDENT UNSUP
%token <str> BLOCKDATA BLOCKSDATA BLOCKASM BLOCKPLAYER BLOCKPF BLOCKPFCOL BLOCKBKCOL

%type <ex> expr lor land cnot cmp bor bxor band shift add mul unary postfix primary optstep dimtarget lval
%type <el> optargs exprlist
%type <cd> cond
%type <st> stmt assignment simple branchsimple
%type <sl> program stmtlist branch branchseq
%type <str> labellist setargs

%start input

%%

input
    : program                  { bc_program = $1; }
    ;

program
    : /* empty */              { $$ = NULL; }
    | program NEWLINE          { $$ = $1; }
    | program stmtlist         { $$ = stl_join($1, $2); }
    ;

stmtlist
    : stmt                     { $$ = stl_one($1); }
    | stmtlist ':'             { $$ = $1; }
    | stmtlist ':' stmt        { $$ = stl_more($1, $3); }
    ;

stmt
    : IDENT                    { $$ = st_new(S_LABEL, LN); $$->s1 = $1; }
    | assignment               { $$ = $1; }
    | simple                   { $$ = $1; }
    ;

assignment
    : LET lval '=' expr        { $$ = st_new(S_ASSIGN, LN); $$->e1 = $2; $$->e2 = $4; }
    | lval '=' expr            { $$ = st_new(S_ASSIGN, LN); $$->e1 = $1; $$->e2 = $3; }
    ;

lval
    : IDENT                    { $$ = ex_id($1, LN); }
    | IDENT '(' expr ')'       { $$ = ex_index($1, $3, LN); }
    | IDENT '{' expr '}'       { $$ = ex_bitread(ex_id($1, LN), $3, LN); }
    ;

simple
    : IF cond THEN branch                      { $$ = st_new(S_IF, LN); $$->cond = $2; $$->body = $4; }
    | IF cond THEN branch ELSE branch          { $$ = st_new(S_IF, LN); $$->cond = $2; $$->body = $4; $$->elsebody = $6; }
    | GOTO IDENT                               { $$ = st_new(S_GOTO, LN); $$->s1 = $2; }
    | GOTO IDENT UNSUP                         { cg_fatal(LN, "'goto ... %s': bank switching not supported", $3); }
    | GOSUB IDENT                              { $$ = st_new(S_GOSUB, LN); $$->s1 = $2; }
    | GOSUB IDENT UNSUP                        { cg_fatal(LN, "'gosub ... %s': bank switching not supported", $3); }
    | RETURNKW                                 { $$ = st_new(S_RETURN, LN); }
    | RETURNKW UNSUP                           { cg_fatal(LN, "'return %s' not supported", $2); }
    | POP                                      { $$ = st_new(S_POP, LN); }
    | ON expr GOTO labellist                   { $$ = st_new(S_ON, LN); $$->e1 = $2; $$->s3 = $4; $$->n2 = 0; }
    | ON expr GOSUB labellist                  { $$ = st_new(S_ON, LN); $$->e1 = $2; $$->s3 = $4; $$->n2 = 1; }
    | FOR IDENT '=' expr TO expr optstep       { $$ = st_new(S_FOR, LN); $$->s1 = $2; $$->e1 = $4; $$->e2 = $6; $$->e3 = $7; }
    | NEXT                                     { $$ = st_new(S_NEXT, LN); }
    | NEXT IDENT                               { $$ = st_new(S_NEXT, LN); $$->s1 = $2; }
    | WHILE cond                               { $$ = st_new(S_WHILE, LN); $$->cond = $2; }
    | WEND                                     { $$ = st_new(S_WEND, LN); }
    | DOKW                                     { $$ = st_new(S_DO, LN); }
    | LOOP                                     { $$ = st_new(S_LOOP, LN); }
    | LOOP WHILE cond                          { $$ = st_new(S_LOOP, LN); $$->cond = $3; $$->n1 = 0; }
    | LOOP UNTIL cond                          { $$ = st_new(S_LOOP, LN); $$->cond = $3; $$->n1 = 1; }
    | EXIT                                     { $$ = st_new(S_EXIT, LN); }
    | DRAWSCREEN                               { $$ = st_new(S_DRAWSCREEN, LN); }
    | REBOOT                                   { $$ = st_new(S_REBOOT, LN); }
    | DIM IDENT '=' dimtarget                  {
                                                   $$ = st_new(S_DIM, LN);
                                                   $$->s1 = $2;
                                                   if ($4->type == EX_NUM) { $$->s2 = NULL; $$->n1 = $4->num; $$->n2 = 1; }
                                                   else { $$->s2 = $4->id; $$->n2 = 0; }
                                               }
    | CONST IDENT '=' expr                     { $$ = st_new(S_CONST, LN); $$->s1 = $2; $$->e1 = $4; }
    | SET setargs                              { $$ = st_new(S_SET, LN); $$->s3 = $2; }
    | DATA IDENT BLOCKDATA                     { $$ = st_new(S_DATA, LN); $$->s1 = $2; $$->s3 = $3; }
    | SDATA IDENT NUMBER BLOCKSDATA            { $$ = st_new(S_SDATA, LN); $$->s1 = $2; $$->s3 = $4; $$->n1 = $3; }
    | SDATA IDENT BLOCKSDATA                   { $$ = st_new(S_SDATA, LN); $$->s1 = $2; $$->s3 = $3; }
    | ASMKW BLOCKASM                           { $$ = st_new(S_ASM, LN); $$->s3 = $2; }
    | PLAYERBLK BLOCKPLAYER                    { $$ = st_new(S_PLAYER, LN); $$->n1 = $1; $$->s3 = $2; }
    | PLAYFIELDBLK BLOCKPF                     { $$ = st_new(S_PF, LN); $$->s3 = $2; }
    | PFCOLORSBLK BLOCKPFCOL                   { $$ = st_new(S_PFCOL, LN); $$->s3 = $2; }
    | BKCOLORSBLK BLOCKBKCOL                   { $$ = st_new(S_BKCOL, LN); $$->s3 = $2; }
    | PFPX expr expr OFF                       { $$ = st_new(S_PFPX, LN);     $$->e1=$2; $$->e2=$3;                 $$->n1 = 1; }
    | PFPX expr expr FLIP                      { $$ = st_new(S_PFPX, LN);     $$->e1=$2; $$->e2=$3;                 $$->n1 = 2; }
    | PFPX expr expr ON                        { $$ = st_new(S_PFPX, LN);     $$->e1=$2; $$->e2=$3;                 $$->n1 = 0; }
    | PFHL expr expr expr OFF                  { $$ = st_new(S_PFHLINE, LN);  $$->e1=$2; $$->e2=$3; $$->e3=$4;      $$->n1 = 1; }
    | PFHL expr expr expr FLIP                 { $$ = st_new(S_PFHLINE, LN);  $$->e1=$2; $$->e2=$3; $$->e3=$4;      $$->n1 = 2; }
    | PFHL expr expr expr ON                   { $$ = st_new(S_PFHLINE, LN);  $$->e1=$2; $$->e2=$3; $$->e3=$4;      $$->n1 = 0; }
    | PFVL expr expr expr OFF                  { $$ = st_new(S_PFVLINE, LN);  $$->e1=$2; $$->e2=$3; $$->e3=$4;      $$->n1 = 1; }
    | PFVL expr expr expr FLIP                 { $$ = st_new(S_PFVLINE, LN);  $$->e1=$2; $$->e2=$3; $$->e3=$4;      $$->n1 = 2; }
    | PFVL expr expr expr ON                   { $$ = st_new(S_PFVLINE, LN);  $$->e1=$2; $$->e2=$3; $$->e3=$4;      $$->n1 = 0; }
    | PFSCROLL UP                              { $$ = st_new(S_PFSCROLL, LN); $$->n1 = 0; }
    | PFSCROLL DOWN                            { $$ = st_new(S_PFSCROLL, LN); $$->n1 = 1; }
    | PFSCROLL LEFT                            { $$ = st_new(S_PFSCROLL, LN); $$->n1 = 2; }
    | PFSCROLL RIGHT                           { $$ = st_new(S_PFSCROLL, LN); $$->n1 = 3; }
    | PFCLEAR                                  { $$ = st_new(S_PFCLEAR, LN); }
    | PFCLEAR expr                             { $$ = st_new(S_PFCLEAR, LN); $$->e1 = $2; }
    | UNSUP                                    { cg_fatal(LN, "'%s' is not supported by bC", $1); }
    ;

branch
    : IDENT                                    { $$ = stl_one(mk_goto($1)); }
    | branchseq                                { $$ = $1; }
    ;

branchseq
    : branchsimple                             { $$ = stl_one($1); }
    | branchseq ':' branchsimple               { $$ = stl_more($1, $3); }
    ;

branchsimple
    : assignment                               { $$ = $1; }
    | IF cond THEN branch                      { $$ = st_new(S_IF, LN); $$->cond = $2; $$->body = $4; }
    | IF cond THEN branch ELSE branch          { $$ = st_new(S_IF, LN); $$->cond = $2; $$->body = $4; $$->elsebody = $6; }
    | GOTO IDENT                               { $$ = st_new(S_GOTO, LN); $$->s1 = $2; }
    | GOSUB IDENT                              { $$ = st_new(S_GOSUB, LN); $$->s1 = $2; }
    | RETURNKW                                 { $$ = st_new(S_RETURN, LN); }
    | POP                                      { $$ = st_new(S_POP, LN); }
    | EXIT                                     { $$ = st_new(S_EXIT, LN); }
    | DRAWSCREEN                               { $$ = st_new(S_DRAWSCREEN, LN); }
    ;

labellist
    : IDENT                                    { $$ = NULL; $$ = cat2(NULL, $1); free($1); }
    | labellist IDENT                          { $$ = cat2($1, $2); free($2); }
    ;

setargs
    : /* empty */                              { $$ = NULL; }
    | setargs IDENT                            { $$ = cat2($1, $2); free($2); }
    | setargs NUMBER                           { char t[32]; snprintf(t, sizeof t, "%ld", $2); $$ = cat2($1, t); }
    | setargs ON                               { $$ = cat2($1, "on"); }
    | setargs OFF                              { $$ = cat2($1, "off"); }
    ;

optstep
    : /* empty */                              { $$ = NULL; }
    | STEP expr                                { $$ = $2; }
    ;

dimtarget
    : IDENT                                    { $$ = ex_id($1, LN); }
    | NUMBER                                   { $$ = ex_num($1, LN); }
    ;

cond
    : expr                                     { $$ = cd_val($1); }
    ;

/* ---- expressions: layered precedence, lowest binds first ---- */

expr
    : lor                                      { $$ = $1; }
    ;

lor
    : land                                     { $$ = $1; }
    | lor OROR land                            { $$ = ex_bin(OROR, $1, $3, LN); }
    ;

land
    : cnot                                     { $$ = $1; }
    | land ANDAND cnot                         { $$ = ex_bin(ANDAND, $1, $3, LN); }
    ;

cnot
    : '!' cnot                                 { $$ = ex_un('!', $2, LN); }
    | cmp                                      { $$ = $1; }
    ;

cmp
    : bor                                      { $$ = $1; }
    | bor '=' bor                              { $$ = ex_bin(EQ, $1, $3, LN); }
    | bor EQ bor                               { $$ = ex_bin(EQ, $1, $3, LN); }
    | bor NE bor                               { $$ = ex_bin(NE, $1, $3, LN); }
    | bor '<' bor                              { $$ = ex_bin(LT, $1, $3, LN); }
    | bor '>' bor                              { $$ = ex_bin(GT, $1, $3, LN); }
    | bor LE bor                               { $$ = ex_bin(LE, $1, $3, LN); }
    | bor GE bor                               { $$ = ex_bin(GE, $1, $3, LN); }
    ;

bor
    : bxor                                     { $$ = $1; }
    | bor '|' bxor                             { $$ = ex_bin('|', $1, $3, LN); }
    ;

bxor
    : band                                     { $$ = $1; }
    | bxor '^' band                            { $$ = ex_bin('^', $1, $3, LN); }
    ;

band
    : shift                                    { $$ = $1; }
    | band '&' shift                           { $$ = ex_bin('&', $1, $3, LN); }
    ;

shift
    : add                                      { $$ = $1; }
    | shift SHL add                            { $$ = ex_bin(SHL, $1, $3, LN); }
    | shift SHR add                            { $$ = ex_bin(SHR, $1, $3, LN); }
    ;

add
    : mul                                      { $$ = $1; }
    | add '+' mul                              { $$ = ex_bin('+', $1, $3, LN); }
    | add '-' mul                              { $$ = ex_bin('-', $1, $3, LN); }
    ;

mul
    : unary                                    { $$ = $1; }
    | mul '*' unary                            { $$ = ex_bin('*', $1, $3, LN); }
    | mul '/' unary                            { $$ = ex_bin('/', $1, $3, LN); }
    | mul MOD unary                            { $$ = ex_bin(MOD, $1, $3, LN); }
    ;

unary
    : '-' unary                                { $$ = ex_un('-', $2, LN); }
    | postfix                                  { $$ = $1; }
    ;

postfix
    : primary                                  { $$ = $1; }
    | postfix '{' expr '}'                     { $$ = ex_bitread($1, $3, LN); }
    ;

primary
    : NUMBER                                   { $$ = ex_num($1, LN); }
    | IDENT                                    { $$ = ex_id($1, LN); }
    | IDENT '(' optargs ')'                    { $$ = ex_call($1, $3, LN); }
    | IDENT '[' expr ']'                       { $$ = ex_index($1, $3, LN); }
    | '(' expr ')'                             { $$ = $2; }
    ;

optargs
    : /* empty */                              { $$ = NULL; }
    | exprlist                                 { $$ = $1; }
    ;

exprlist
    : expr                                     { $$ = exl_one($1); }
    | exprlist ',' expr                        { $$ = exl_more($1, $3); }
    ;

%%

int yyerror(const char *s)
{
    extern FILE *yyin;
    (void)yyin;
    fprintf(stderr, "bC: %s:%d: %s\n", "?", bcline, s);
    exit(1);
}

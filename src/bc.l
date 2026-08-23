/* bC -- batari Basic to C transpiler. Lexer. */

%{
#include <stdlib.h>
#include <string.h>
#include "codegen.h"
#include "bc.tab.h"

int bcline = 1;

static char *blk = NULL;         /* block payload accumulator */
static size_t blklen = 0, blkcap = 0;
static int pending = -1;         /* block state to enter after header line */
static int blkkind = 0;          /* S_DATA/S_SDATA/S_ASM/S_PLAYER/S_PF/S_PFCOL/S_BKCOL */
static long playerno = 0;

static void blk_reset(void) { blklen = 0; if (blk) blk[0] = 0; }
static void blk_put(const char *s)
{
    size_t n;
    if (!s) return;
    n = strlen(s);
    while (blkcap < blklen + n + 1) { blkcap = blkcap ? blkcap * 2 : 4096; blk = realloc(blk, blkcap); }
    memcpy(blk + blklen, s, n); blklen += n; blk[blklen] = 0;
}
/* returns 1 and terminates block when the line is just "end" (ws/comment allowed) */
static int blk_is_end(const char *s)
{
    const char *p = s;
    int gotend = 0;
    while (*p == ' ' || *p == '\t' || *p == '\r') p++;
    if ((p[0] == 'e' || p[0] == 'E') && !strncasecmp(p, "end", 3)) {
        p += 3;
        while (*p == ' ' || *p == '\t' || *p == '\r') p++;
        if (*p == 0 || *p == '\n' || *p == ';' ||
            (!strncasecmp(p, "rem", 3))) gotend = 1;
    }
    return gotend;
}

void yyerror(const char *s);
%}

%option noyywrap nounput noinput
%option case-insensitive

%x BLK_BODY

%%

[ \t\r]+                ;
"\n"                    {
                          bcline++;
                          if (pending >= 0) {
                              /* swallow newline that opens a block body */
                              BEGIN(BLK_BODY); blk_reset(); pending = -1;
                          } else {
                              return NEWLINE;
                          }
                        }
";"[^\n]*               ;
"rem"[ \t][^\n]*        ;
"rem\n"                 ;
"/*"([^*]|\*+[^*/])*\*+"/"   ;

"$"[0-9a-fA-F]+         { yylval.num = strtol(yytext+1, NULL, 16); return NUMBER; }
"%"[01]+                { yylval.num = strtol(yytext+1, NULL, 2);  return NUMBER; }
[0-9]+                  { yylval.num = atoi(yytext); return NUMBER; }

"data"                  { blkkind = S_DATA;  pending = BLK_BODY; return DATA; }
"sdata"                 { blkkind = S_SDATA; pending = BLK_BODY; return SDATA; }
"asm"                   { blkkind = S_ASM;   pending = BLK_BODY; return ASMKW; }
"player"[0-9]+":"       {
                          playerno = atol(yytext + 6);
                          yytext[strlen(yytext)-1] = 0;
                          yylval.num = playerno;
                          blkkind = S_PLAYER; pending = BLK_BODY;
                          return PLAYERBLK;
                        }
"playfield:"            { blkkind = S_PF;     pending = BLK_BODY; return PLAYFIELDBLK; }
"pfcolors:"             { blkkind = S_PFCOL;  pending = BLK_BODY; return PFCOLORSBLK; }
"bkcolors:"             { blkkind = S_BKCOL;  pending = BLK_BODY; return BKCOLORSBLK; }

<BLK_BODY>{
[^\n]*\n                {
                          bcline++;
                          if (blk_is_end(yytext)) {
                              char *payload;
                              BEGIN(INITIAL);
                              if (blklen && blk[blklen-1] == '\n') blk[--blklen] = 0;
                              payload = bc_strdup(blk ? blk : "");
                              switch (blkkind) {
                              case S_DATA:   yylval.str = payload; return BLOCKDATA;
                              case S_SDATA:  yylval.str = payload; return BLOCKSDATA;
                              case S_ASM:    yylval.str = payload; return BLOCKASM;
                              case S_PLAYER: yylval.str = payload; return BLOCKPLAYER;
                              case S_PF:     yylval.str = payload; return BLOCKPF;
                              case S_PFCOL:  yylval.str = payload; return BLOCKPFCOL;
                              case S_BKCOL:  yylval.str = payload; return BLOCKBKCOL;
                              default:       cg_fatal(bcline, "internal: bad block kind");
                              }
                          } else {
                              /* keep line but strip trailing ;comment / rem comment */
                              char buf[2048];
                              char *c;
                              snprintf(buf, sizeof(buf), "%s", yytext);
                              c = strchr(buf, ';');  if (c) *c = 0;
                              c = strcasestr(buf, "rem"); if (c && (c == buf || c[-1]==' ')) *c = 0;
                              blk_put(buf);
                          }
                        }
<<EOF>>                 { cg_fatal(bcline, "unexpected end of file inside a block (missing 'end')"); }
}

if                      return IF;
then                    return THEN;
else                    return ELSE;
goto                    return GOTO;
gosub                   return GOSUB;
return                  return RETURNKW;
pop                     return POP;
on                      return ON;
off                     return OFF;
flip                    return FLIP;
up                      return UP;
down                    return DOWN;
left                    return LEFT;
right                   return RIGHT;
until                   return UNTIL;
for                     return FOR;
to                      return TO;
step                    return STEP;
next                    return NEXT;
while                   { return WHILE; }
wend                    { return WEND; }
do                      { return DOKW; }
loop                    { return LOOP; }
exit                    { return EXIT; }
drawscreen              { return DRAWSCREEN; }
reboot                  { return REBOOT; }
let                     { return LET; }
dim                     { return DIM; }
const                   { return CONST; }
set                     { return SET; }
pfpixel                 { return PFPX; }
pfhline                 { return PFHL; }
pfvline                 { return PFVL; }
pfscroll                { return PFSCROLL; }
pfclear                 { return PFCLEAR; }
include|inline|macro|function|bank|push|pull|thisbank|otherbank {
                          yylval.id = bc_strdup_lower(yytext); return UNSUP;
                        }

"<<"                    return SHL;
">>"                    return SHR;
"<="                    return LE;
">="                    return GE;
"<>"                    return NE;
"=="                    return EQ;
"!="                    return NE;
"&&"                    return ANDAND;
"||"                    return OROR;
"//"                    return MOD;

[A-Za-z_][A-Za-z0-9_]*  { yylval.id = bc_strdup_lower(yytext); return IDENT; }
.                       { return yytext[0]; }

%%

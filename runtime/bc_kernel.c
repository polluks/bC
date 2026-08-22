/* bC runtime kernel.
 * This file is #included into the transpiler's generated C file (single
 * translation unit) so it can see BC_PAL and the TIA/RIOT declarations.
 * Frame timing (scanlines):        vsync vblank visible overscan
 *   NTSC:                            3     37     192      30
 *   PAL:                             3     45     228      36
 */
#ifndef BC_KERNEL_C
#define BC_KERNEL_C

#include <string.h>

#if !defined(BC_PAL)
#define BC_VBLANK_LINES 37
#define BC_VIS_LINES    192
#define BC_OVERSCAN_LINES 30
#else
#define BC_VBLANK_LINES 45
#define BC_VIS_LINES    228
#define BC_OVERSCAN_LINES 36
#endif

/* first scanline (within visible area) of playfield row 0; 11 rows x 8
 * lines = 88 lines, roughly centered */
#define BC_PF_TOP 52

/* ---- storage ---- */
unsigned char bc_pf[44];       /* 11 rows x [PF0 PF1 PF2 0] */
unsigned char bc_p0gfx[8];
unsigned char bc_p1gfx[8];
unsigned char bc_score[6];     /* decimal digits, [0] leftmost */
unsigned char bc_scorecolor;
unsigned char player0x, player0y;
unsigned char player1x, player1y;
unsigned char temp1, temp2, temp3, temp4, temp5, temp6, temp7;

const unsigned char *bc_pfcolors = 0;
const unsigned char *bc_bkcolors = 0;

/* ---- rand: 16-bit LFSR ---- */
static unsigned int bc_lfsr = 1;

static void bc_rand_step(void)
{
    /* taps 16,14,13,11 */
    unsigned int bit = ((bc_lfsr >> 15) ^ (bc_lfsr >> 13) ^
                        (bc_lfsr >> 12) ^ (bc_lfsr >> 10)) & 1u;
    bc_lfsr = (bc_lfsr << 1) | bit;
}

unsigned char bc_rand(void)   { bc_rand_step(); return (unsigned char)(bc_lfsr & 0xFF); }
#ifndef BC_NO_RAND16
unsigned char bc_rand16(void) { unsigned char r; int i; for (i=0;i<8;i++) bc_rand_step(); r=(unsigned char)(bc_lfsr&0xFF); for(i=0;i<8;i++) bc_rand_step(); return (unsigned char)((bc_lfsr & 0xFF) ^ r); }
#endif

void bc_init(void)
{
    memset(bc_pf, 0, sizeof bc_pf);
    memset(bc_p0gfx, 0, sizeof bc_p0gfx);
    memset(bc_p1gfx, 0, sizeof bc_p1gfx);
    memset(bc_score, 0, sizeof bc_score);
    bc_scorecolor = 0x0E;
    player0x = player0y = 0;
    player1x = player1y = 0;
    RIOT.swacnt = 0;               /* console switches read */
    RIOT.swcha  = 0xFF;            /* joysticks: input */
    TIA.colubk  = 0;
    TIA.ctrlpf  = 0x01;            /* playfield reflected */
    /* seed from timer decay if possible */
    bc_lfsr = (unsigned)(RIOT.intim << 8 | RIOT.timint);
    if (!bc_lfsr) bc_lfsr = 0xBEEF;
}

/* ---- score (BCD-ish decimal helpers; digits array, not rendered) ---- */
void bc_score_set(unsigned char v)
{
    int i;
    unsigned val = v;
    for (i = 0; i < 6; i++) bc_score[i] = 0;
    for (i = 5; i >= 0 && val; i--) { bc_score[i] = val % 10; val /= 10; }
}
#ifndef BC_NO_SCORE_ADD
void bc_score_add(unsigned char v)
{
    int i, carry = 0;
    unsigned val = v;
    for (i = 5; i >= 0 && (val || carry); i--) {
        int dgt = bc_score[i] + (val % 10) + carry;
        if (dgt > 9) { dgt -= 10; carry = 1; } else carry = 0;
        bc_score[i] = dgt;
        val /= 10;
    }
}
#endif
#ifndef BC_NO_SCORE_SUB
void bc_score_sub(unsigned char v)
{
    int i, d, borrow = 0;
    for (i = 5; i >= 0 && (v || borrow); i--) {
        d = bc_score[i] - (v % 10) - borrow;
        if (d < 0) { d += 10; borrow = 1; } else borrow = 0;
        bc_score[i] = d; v /= 10;
    }
}
#endif

/* ---- playfield pixel mapping (32 logical columns, mirrored) ----
 * col 0-3   -> PF0 bit(7-col)          (high nibble only)
 * col 4-11  -> PF1 bit(col-4)          (reversed byte order)
 * col 12-19 -> PF2 bit(19-col)
 * col 20-31 -> mirror to 31-col, then as above
 */
static void pf_bitpos(int c, int *byteidx, unsigned char *mask)
{
    unsigned char b, m;
    if (c < 0) c = 0;
    if (c > 31) c = 31;
    if (c >= 20) c = 39 - c;           /* mirror right half onto left registers */
    if (c < 4)       { b = 0; m = 0x80 >> c; }        /* PF0 high nibble */
    else if (c < 12) { b = 1; m = 0x01 << (c - 4); }  /* PF1 reversed */
    else             { b = 2; m = 0x01 << (c - 12); } /* PF2 */
    *byteidx = b;
    *mask = m;
}

void bc_pfpixel(unsigned char x, unsigned char y, unsigned char mode)
{
    int b, row = y >> 3; unsigned char m;
    if (row > 10 || x > 31) return;
    pf_bitpos(x, &b, &m);
    if (mode == 0)      bc_pf[row*4+b] |= m;
    else if (mode == 1) bc_pf[row*4+b] &= ~m;
    else                bc_pf[row*4+b] ^= m;
}

#ifndef BC_NO_PFEXTRA
unsigned char bc_pfread(unsigned char x, unsigned char y)
{
    int b, row = y >> 3; unsigned char m;
    if (row > 10 || x > 31) return 0;
    pf_bitpos(x, &b, &m);
    return (bc_pf[row*4+b] & m) ? 1 : 0;
}
#endif

#ifndef BC_NO_PFEXTRA
void bc_pfhline(unsigned char y, unsigned char x1, unsigned char x2, unsigned char mode)
{
    unsigned char x;
    if (x1 > x2) { unsigned char t = x1; x1 = x2; x2 = t; }
    for (x = x1; ; x++) { bc_pfpixel(x, y, mode); if (x == x2) break; }
}

void bc_pfvline(unsigned char x, unsigned char y1, unsigned char y2, unsigned char mode)
{
    unsigned char y;
    if (y1 > y2) { unsigned char t = y1; y1 = y2; y2 = t; }
    for (y = y1; ; y++) { bc_pfpixel(x, y, mode); if (y == y2) break; }
}
#endif /* BC_NO_PFEXTRA */

#ifndef BC_NO_PFEXTRA
void bc_pfclear(unsigned char v)
{
    int i;
    for (i = 0; i < 44; i++)
        bc_pf[i] = (i & 3) == 3 ? 0 : v;   /* keep pad bytes zero */
}
#endif

#ifndef BC_NO_PFEXTRA
void bc_pfscroll(unsigned char dir)
{
    if (dir == 0) {                       /* up */
        memmove(bc_pf, bc_pf+4, 40);
        memset(bc_pf+40, 0, 4);
    } else if (dir == 1) {                /* down */
        memmove(bc_pf+4, bc_pf, 40);
        memset(bc_pf, 0, 4);
    } else {
        int r, c;
        unsigned char edge;
        for (r = 0; r < 11; r++) {
            if (dir == 2) {               /* left: capture col 0 */
                edge = bc_pfread(0, r << 3);
                for (c = 0; c < 31; c++)
                    bc_pfpixel(c, r << 3, bc_pfread(c+1, r << 3));
                bc_pfpixel(31, r << 3, edge);
            } else {                      /* right */
                edge = bc_pfread(31, r << 3);
                for (c = 31; c > 0; c--)
                    bc_pfpixel(c, r << 3, bc_pfread(c-1, r << 3));
                bc_pfpixel(0, r << 3, edge);
            }
        }
    }
}
#endif /* BC_NO_PFEXTRA */

/* ---- player positioning during vblank (approximate) ----
 * Coarse delay loop then RESP strobe. Resolution ~3 pixels; good enough
 * for simple kernels and keeps cc65 output small. */
static void bc_pos_player(unsigned char *resp, unsigned char x)
{
    unsigned char d;
    TIA.wsync = 0;
    d = (unsigned char)(((unsigned)x + 44u) / 12u);   /* iterations of 4-cycle nops */
    while (d--) { __asm__ ("\tnop"); __asm__ ("\tnop"); }
    *resp = 0;
}

void bc_drawscreen(void)
{
    int i, l;
    unsigned char row = 0xFF;
    unsigned char py0, py1;

    /* vertical sync: 3 lines */
    TIA.vsync = 0x02;
    for (i = 0; i < 3; i++) TIA.wsync = 0;
    TIA.vsync = 0;

    /* vertical blank */
    TIA.vblank = 0x02;
    bc_pos_player(&TIA.resp0, player0x);
    bc_pos_player(&TIA.resp1, player1x);
    py0 = player0y;
    py1 = player1y;
    for (i = 0; i < BC_VBLANK_LINES; i++) TIA.wsync = 0;
    TIA.vblank = 0;

    /* visible area */
    for (l = 0; l < BC_VIS_LINES; l++) {
        unsigned char g0 = 0, g1 = 0;
        TIA.wsync = 0;
        if (l >= BC_PF_TOP && l < BC_PF_TOP + 88) {
            unsigned char r = (l - BC_PF_TOP) >> 3;
            if (r != row) {
                row = r;
                TIA.pf0 = bc_pf[row*4+0];
                TIA.pf1 = bc_pf[row*4+1];
                TIA.pf2 = bc_pf[row*4+2];
                if (bc_pfcolors) TIA.colupf = bc_pfcolors[row];
                if (bc_bkcolors) TIA.colubk = bc_bkcolors[row];
            }
        }
        if (l >= py0 && l < py0 + 16) g0 = bc_p0gfx[(l - py0) >> 1];
        if (l >= py1 && l < py1 + 16) g1 = bc_p1gfx[(l - py1) >> 1];
        TIA.grp0 = g0;
        TIA.grp1 = g1;
    }
    TIA.grp0 = 0;
    TIA.grp1 = 0;

    /* overscan */
    TIA.vblank = 0x02;
    for (i = 0; i < BC_OVERSCAN_LINES; i++) TIA.wsync = 0;
}

#endif /* BC_KERNEL_C */

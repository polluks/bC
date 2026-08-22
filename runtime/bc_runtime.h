/* bC runtime -- declarations visible to transpiled programs.
 * The Atari 2600 target provides TIA/RIOT structs via <atari2600.h>.
 */
#ifndef BC_RUNTIME_H
#define BC_RUNTIME_H

/* gosub machinery limits */
#define BC_GOSUB_DEPTH 8

/* ---- kernel-owned RAM ---- */
extern unsigned char bc_pf[44];        /* playfield: 11 rows x 4 bytes */
extern unsigned char bc_p0gfx[8];      /* player 0 shape (RAM, user-modifiable) */
extern unsigned char bc_p1gfx[8];
extern unsigned char bc_score[6];      /* six decimal digits, bc_score[0] = leftmost */
extern unsigned char bc_scorecolor;
extern unsigned char player0x, player0y;
extern unsigned char player1x, player1y;
extern unsigned char temp1, temp2, temp3, temp4, temp5, temp6, temp7;

/* optional per-row color tables (NULL = leave registers alone) */
extern const unsigned char *bc_pfcolors;
extern const unsigned char *bc_bkcolors;

/* ---- kernel entry points ---- */
void bc_init(void);
void bc_drawscreen(void);
unsigned char bc_rand(void);
unsigned char bc_rand16(void);

void bc_score_set(unsigned char v);
void bc_score_add(unsigned char v);
void bc_score_sub(unsigned char v);

void bc_pfpixel(unsigned char x, unsigned char y, unsigned char mode); /* 0=on 1=off 2=flip */
void bc_pfhline(unsigned char y, unsigned char x1, unsigned char x2, unsigned char mode);
void bc_pfvline(unsigned char x, unsigned char y1, unsigned char y2, unsigned char mode);
unsigned char bc_pfread(unsigned char x, unsigned char y);
void bc_pfscroll(unsigned char dir);   /* 0=up 1=down 2=left 3=right */
void bc_pfclear(unsigned char v);

/* ---- joystick / console helpers (SWCHA bits are active-low) ---- */
#define joy0right (!(RIOT.swcha & 0x80))
#define joy0left  (!(RIOT.swcha & 0x40))
#define joy0down  (!(RIOT.swcha & 0x20))
#define joy0up    (!(RIOT.swcha & 0x10))
#define joy0fire  (!(TIA.inpt4 & 0x80))
#define joy1right (!(RIOT.swcha & 0x08))
#define joy1left  (!(RIOT.swcha & 0x04))
#define joy1down  (!(RIOT.swcha & 0x02))
#define joy1up    (!(RIOT.swcha & 0x01))
#define joy1fire  (!(TIA.inpt5 & 0x80))

#endif /* BC_RUNTIME_H */

/* Host-side mock of <atari2600.h> for bC logic tests.
 * Same member names as the cc65 headers but plain RAM-backed globals,
 * so transpiled programs run natively on the build host. */
#ifndef MOCK_ATARI2600_H
#define MOCK_ATARI2600_H

struct __tia {
    unsigned char vsync, vblank, wsync, rsync;
    unsigned char nusiz0, nusiz1, colup0, colup1;
    unsigned char colupf, colubk, ctrlpf, refp0, refp1;
    unsigned char pf0, pf1, pf2, resp0, resp1, resm0, resm1, resbl;
    unsigned char audc0, audc1, audf0, audf1, audv0, audv1;
    unsigned char grp0, grp1, enam0, enam1, enabl;
    unsigned char hmp0, hmp1, hmm0, hmm1, hmbl;
    unsigned char vdelp0, vdelp1, vdelbl, resmp0, resmp1, hmove, hmclr, cxclr;
    unsigned char cxm0p, cxm1p, cxp0fb, cxp1fb, cxm0fb, cxm1fb, cxblpf, cxppmm;
    unsigned char inpt0, inpt1, inpt2, inpt3, inpt4, inpt5;
};

struct __riot {
    unsigned char swcha, swacnt, swchb, swbcnt, intim, timint;
    unsigned char unused[14];
    unsigned char tim1t, tim8t, tim64t, t1024t;
};

extern struct __tia TIA_i;
extern struct __riot RIOT_i;

#define TIA  TIA_i
#define RIOT RIOT_i

#endif

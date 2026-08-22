/* Host driver for bC transpiled output: runs the generated main (renamed
 * via -Dmain=bC_main), scripts joystick/collisions per frame, then checks
 * observable program variables after the frame cap ends the run. */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "atari2600.h"

struct __tia TIA_i;
struct __riot RIOT_i;

unsigned bc_max_frames = 200;
static int fails = 0;

/* program variables exercised by test/host.bas */
extern unsigned char m, c, d, e, f, h, t, n, o, p, q, r, w, x, y, z, v;

void bc_frame_hook(unsigned fr)
{
    RIOT_i.swcha = 0xFF;            /* all directions released */
    TIA_i.inpt4  = 0x80;            /* fire released */
    TIA_i.cxppmm = 0;               /* no P0/P1 collision */
    if (fr >= 10 && fr <= 20) RIOT_i.swcha &= ~0x80;   /* joy0right */
    if (fr >= 30 && fr <= 32) TIA_i.cxppmm = 0x80;     /* P0 vs P1 */
}

static void chk(const char *name, long got, long want)
{
    int ok = got == want;
    printf("%-24s %-4s (got %ld, want %ld)\n", name, ok ? "PASS" : "FAIL", got, want);
    if (!ok) fails++;
}

static void report(void)
{
    chk("gosub chain d", d, 100);
    chk("nested gosub e", e, 11);
    chk("innermost f", f, 1);
    chk("on-gosub range h", h, 1);
    chk("on-gosub return t", t, 5);
    chk("on-goto skip n", n, 1);
    chk("on-goto pick o", o, 50);
    chk("for/step p", p, 4);
    chk("for final q", q, 13);
    chk("do-until r", r, 0);
    chk("while w", w, 0);
    chk("do-exit x", x, 3);
    chk("pfread y", y, 0);
    chk("sread[0] z", z, 10);
    chk("sread[1] v", v, 20);
    {
        extern unsigned char bc_score[6];
        chk("score 250+250", bc_score[3], 5);
        chk("score high digits", bc_score[0] + bc_score[1] + bc_score[2], 0);
        chk("score low digits", bc_score[4] + bc_score[5], 0);
    }
    chk("joy0right frames m", m, 11);
    chk("collision frames c", c, 3);

    printf("\n%s\n", fails ? "SOME CHECKS FAILED" : "ALL CHECKS PASSED");
    fflush(stdout);
    if (fails) _exit(1);   /* _exit: don't re-enter atexit */
}

int main(void)
{
    extern int bC_main(void);
    atexit(report);
    bC_main();                      /* exits via frame cap */
    return 0;
}

/* Frame-counting shim: pulls in the real kernel, then wraps drawscreen
 * so host tests terminate after a bounded number of frames. */
#ifndef BC_MOCK_KERNEL
#define BC_MOCK_KERNEL

#include <stdlib.h>

extern void bc_frame_hook(unsigned frame);
extern unsigned bc_max_frames;

#define bc_init       bc_real_init
#define bc_drawscreen bc_real_drawscreen
#include "../../runtime/bc_kernel.c"
#undef bc_init
#undef bc_drawscreen

void bc_init(void)
{
    bc_real_init();
}

void bc_drawscreen(void)
{
    static unsigned f = 0;
    bc_real_drawscreen();
    bc_frame_hook(++f);
    if (f >= bc_max_frames) exit(0);
}

#endif /* BC_MOCK_KERNEL */

#define main kiosk_program_main
#include "nx679j-kiosk2.c"
#undef main

int main(void)
{
    /* 3 visible XRGB pixels plus one padding pixel per 16-byte row. */
    const u32 pitch_pixels = 4;
    const u32 marker = 0x00123456;
    u32 fb[8] = { 0 };
    int failed = 0;

    W = 3;
    H = 2;
    fill_rect(fb, pitch_pixels * sizeof(u32), 0, 1, 3, 1, marker);

    for (u32 x = 0; x < 3; x++) {
        if (fb[pitch_pixels + x] != marker) {
            fprintf(stderr, "RED: row 1 pixel %u is %#x, expected %#x\n",
                    x, fb[pitch_pixels + x], marker);
            failed = 1;
        }
    }
    if (fb[3] != 0) {
        fprintf(stderr, "RED: row padding was overwritten with %#x\n", fb[3]);
        failed = 1;
    }

    return failed;
}

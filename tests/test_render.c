#include "render.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

int main(void)
{
    const uint8_t pixels[] = {0, 255, 128, 64};
    char out[128];
    size_t n = moqy_ascii_frame(pixels, 2, 2, 2, 2, 2, out, sizeof(out));
    assert(n == 6);
    assert(strcmp(out, " @\n=:\n") == 0);

    const int16_t quiet[] = {0, 0, 0, 0};
    const int16_t loud[] = {32767, -32768, 32767, -32768};
    moqy_waveform(quiet, 4, 4, out, sizeof(out));
    assert(strcmp(out, "    \n    \n    \n    \n====\n") == 0);
    moqy_waveform(loud, 4, 4, out, sizeof(out));
    assert(strcmp(out, "||||\n||||\n||||\n||||\n====\n") == 0);
    return 0;
}

#include "render.h"

#include <limits.h>

size_t moqy_ascii_frame(const uint8_t *gray, int width, int height, int stride,
                        int columns, int rows, char *out, size_t capacity)
{
    static const char shades[] = " .:-=+*#%@";
    if (!gray || !out || width <= 0 || height <= 0 || stride < width ||
        columns <= 0 || rows <= 0 ||
        (size_t)columns + 1 > (SIZE_MAX - 1) / (size_t)rows)
        return 0;
    size_t needed = ((size_t)columns + 1) * (size_t)rows;
    if (needed >= capacity)
        return 0;
    size_t pos = 0;
    for (int y = 0; y < rows; ++y) {
        int sy = (int)((int64_t)y * height / rows);
        for (int x = 0; x < columns; ++x) {
            int sx = (int)((int64_t)x * width / columns);
            out[pos++] = shades[gray[(size_t)sy * stride + sx] * 9 / 255];
        }
        out[pos++] = '\n';
    }
    out[pos] = '\0';
    return pos;
}

size_t moqy_waveform(const int16_t *samples, size_t count, int columns,
                     char *out, size_t capacity)
{
    if (!out || columns <= 0 || (size_t)columns > (SIZE_MAX - 1) / 5 ||
        ((size_t)columns + 1) * 5 >= capacity)
        return 0;
    size_t pos = 0;
    for (int row = 0; row < 5; ++row) {
        for (int x = 0; x < columns; ++x) {
            uint32_t peak = 0;
            if (samples && count) {
                size_t start = (size_t)x * count / (size_t)columns;
                size_t end = (size_t)(x + 1) * count / (size_t)columns;
                if (end <= start && start < count) end = start + 1;
                for (size_t i = start; i < end; ++i) {
                    int32_t v = samples[i];
                    uint32_t abs_v = (uint32_t)(v < 0 ? -v : v);
                    if (abs_v > peak) peak = abs_v;
                }
            }
            unsigned bars = (unsigned)(((uint64_t)peak * 4 + 32767) / 32768);
            if (bars > 4) bars = 4;
            out[pos++] = row == 4 ? '=' : (bars >= (unsigned)(4 - row) ? '|' : ' ');
        }
        out[pos++] = '\n';
    }
    out[pos] = '\0';
    return pos;
}

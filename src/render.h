#ifndef MOQY_RENDER_H
#define MOQY_RENDER_H

#include <stddef.h>
#include <stdint.h>

/* Returns bytes written, or zero when the destination is too small. */
size_t moqy_ascii_frame(const uint8_t *gray, int width, int height, int stride,
                        int columns, int rows, char *out, size_t capacity);
size_t moqy_waveform(const int16_t *samples, size_t count, int columns,
                     char *out, size_t capacity);

#endif

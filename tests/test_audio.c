#include "decode.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t sample_count;
static int peak;

static void on_audio(void *ctx, const int16_t *samples, size_t count)
{
    (void)ctx;
    sample_count += count;
    for (size_t i = 0; i < count; ++i) {
        int value = samples[i];
        if (value < 0) value = -value;
        if (value > peak) peak = value;
    }
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    FILE *file = fopen(argv[1], "rb");
    assert(file != NULL);
    assert(fseek(file, 0, SEEK_END) == 0);
    long length = ftell(file);
    assert(length > 0);
    rewind(file);
    uint8_t *bytes = malloc((size_t)length);
    assert(bytes != NULL);
    assert(fread(bytes, 1, (size_t)length, file) == (size_t)length);
    fclose(file);

    moq_media_track_desc_t desc = {0};
    static const char codec[] = "mp4a.40.2";
    desc.info.media_type = MOQ_MEDIA_TYPE_AUDIO;
    desc.codec.data = (const uint8_t *)codec;
    desc.codec.len = strlen(codec);
    moqy_decoder_t *decoder = moqy_decoder_create(&desc, NULL, on_audio, NULL);
    assert(decoder != NULL);
    moq_media_object_t object = {0};
    object.payload.data = bytes;
    object.payload.len = (size_t)length;
    assert(moqy_decoder_feed(decoder, &object) == 0);
    assert(sample_count > 0);
    assert(peak > 0);
    moqy_decoder_destroy(decoder);
    free(bytes);
    return 0;
}

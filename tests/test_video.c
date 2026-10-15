#include "decode.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int frame_count;

static void on_video(void *ctx, const uint8_t *gray,
                     int width, int height, int stride)
{
    (void)ctx;
    assert(width == 16 && height == 16);
    assert(gray[8 * stride + 8] > 180);
    frame_count++;
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
    static const char codec[] = "avc1.42e01e";
    desc.info.media_type = MOQ_MEDIA_TYPE_VIDEO;
    desc.codec.data = (const uint8_t *)codec;
    desc.codec.len = strlen(codec);
    moqy_decoder_t *decoder = moqy_decoder_create(&desc, on_video, NULL, NULL);
    assert(decoder != NULL);
    moq_media_object_t object = {0};
    object.payload.data = bytes;
    object.payload.len = (size_t)length;
    object.keyframe = true;
    assert(moqy_decoder_feed(decoder, &object) == 0);
    assert(frame_count == 1);
    moqy_decoder_destroy(decoder);
    free(bytes);
    return 0;
}

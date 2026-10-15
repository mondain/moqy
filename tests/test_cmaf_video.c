#include "decode.h"

#include <moq/cmaf.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int frames;

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static void on_video(void *ctx, const uint8_t *gray,
                     int width, int height, int stride)
{
    (void)ctx;
    assert(width == 16 && height == 16);
    assert(gray[8 * stride + 8] > 180);
    frames++;
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

    size_t moof = 0;
    size_t end = 0;
    for (size_t p = 0; p + 8 <= (size_t)length;) {
        uint32_t size = be32(bytes + p);
        assert(size >= 8 && size <= (size_t)length - p);
        if (!memcmp(bytes + p + 4, "moof", 4)) moof = p;
        if (moof && !memcmp(bytes + p + 4, "mdat", 4)) {
            end = p + size;
            break;
        }
        p += size;
    }
    assert(moof > 0 && end > moof);
    moq_cmaf_init_info_t init;
    moq_cmaf_init_info_init(&init);
    assert(moq_cmaf_parse_init((moq_bytes_t){bytes, moof}, &init) == MOQ_OK);
    moq_cmaf_sample_t samples[8];
    moq_cmaf_fragment_info_t fragment;
    moq_cmaf_fragment_info_init(&fragment, samples, 8);
    moq_bytes_t fragment_bytes = {bytes + moof, end - moof};
    assert(moq_cmaf_parse_fragment(fragment_bytes, &fragment) == MOQ_OK);
    assert(fragment.sample_count == 1);

    moq_media_track_desc_t desc = {0};
    desc.info.media_type = MOQ_MEDIA_TYPE_VIDEO;
    desc.init = init;
    moqy_decoder_t *decoder = moqy_decoder_create(&desc, on_video, NULL, NULL);
    assert(decoder != NULL);
    moq_media_object_t object = {0};
    object.packaging = MOQ_MEDIA_PACKAGING_CMAF;
    object.fragment = fragment_bytes;
    object.mdat_offset = (size_t)(fragment.mdat.data - fragment_bytes.data);
    object.mdat_len = fragment.mdat.len;
    object.samples = samples;
    object.sample_count = fragment.sample_count;
    object.keyframe = true;
    assert(moqy_decoder_feed(decoder, &object) == 0);
    assert(frames == 1);
    moqy_decoder_destroy(decoder);
    free(bytes);
    return 0;
}

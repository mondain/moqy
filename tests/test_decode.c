#include "decode.h"

#include <assert.h>
#include <string.h>

int main(void)
{
    moq_media_track_desc_t desc = {0};
    static const char codec[] = "avc1.64001f";
    desc.info.media_type = MOQ_MEDIA_TYPE_VIDEO;
    desc.codec.data = (const uint8_t *)codec;
    desc.codec.len = strlen(codec);
    moqy_decoder_t *decoder = moqy_decoder_create(&desc, NULL, NULL, NULL);
    assert(decoder != NULL);
    moqy_decoder_destroy(decoder);

    static const char unknown[] = "made-up";
    desc.codec.data = (const uint8_t *)unknown;
    desc.codec.len = strlen(unknown);
    assert(moqy_decoder_create(&desc, NULL, NULL, NULL) == NULL);
    return 0;
}

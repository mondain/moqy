#ifndef MOQY_DECODE_H
#define MOQY_DECODE_H

#include <moq/media_receiver.h>
#include <stddef.h>
#include <stdint.h>

typedef struct moqy_decoder moqy_decoder_t;
typedef void (*moqy_video_fn)(void *ctx, const uint8_t *gray,
                              int width, int height, int stride);
typedef void (*moqy_audio_fn)(void *ctx, const int16_t *samples, size_t count);

moqy_decoder_t *moqy_decoder_create(const moq_media_track_desc_t *desc,
                                    moqy_video_fn video, moqy_audio_fn audio,
                                    void *ctx);
int moqy_decoder_feed(moqy_decoder_t *decoder, const moq_media_object_t *obj);
void moqy_decoder_destroy(moqy_decoder_t *decoder);

#endif

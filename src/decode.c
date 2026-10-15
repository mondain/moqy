#include "decode.h"

#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/imgutils.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>

#include <limits.h>
#include <stdlib.h>
#include <string.h>

struct moqy_decoder {
    AVCodecContext *codec;
    AVFrame *frame;
    struct SwsContext *scale;
    SwrContext *resample;
    uint8_t *gray;
    size_t gray_cap;
    int16_t *pcm;
    size_t pcm_cap;
    moqy_video_fn video;
    moqy_audio_fn audio;
    void *ctx;
    moq_media_type_t type;
};

static enum AVCodecID codec_id(moq_cmaf_codec_kind_t kind)
{
    switch (kind) {
    case MOQ_CMAF_CODEC_AVC: return AV_CODEC_ID_H264;
    case MOQ_CMAF_CODEC_HEVC: return AV_CODEC_ID_HEVC;
    case MOQ_CMAF_CODEC_AV1: return AV_CODEC_ID_AV1;
    case MOQ_CMAF_CODEC_AAC: return AV_CODEC_ID_AAC;
    case MOQ_CMAF_CODEC_OPUS: return AV_CODEC_ID_OPUS;
    default: return AV_CODEC_ID_NONE;
    }
}

static bool codec_prefix(moq_bytes_t codec, const char *prefix)
{
    size_t len = strlen(prefix);
    return codec.data && codec.len >= len &&
           memcmp(codec.data, prefix, len) == 0;
}

static enum AVCodecID codec_id_from_name(moq_bytes_t codec)
{
    if (codec_prefix(codec, "avc1") || codec_prefix(codec, "avc3"))
        return AV_CODEC_ID_H264;
    if (codec_prefix(codec, "hvc1") || codec_prefix(codec, "hev1"))
        return AV_CODEC_ID_HEVC;
    if (codec_prefix(codec, "av01")) return AV_CODEC_ID_AV1;
    if (codec_prefix(codec, "mp4a")) return AV_CODEC_ID_AAC;
    if (codec_prefix(codec, "opus") || codec_prefix(codec, "Opus"))
        return AV_CODEC_ID_OPUS;
    return AV_CODEC_ID_NONE;
}

static int handle_frame(moqy_decoder_t *d)
{
    AVFrame *f = d->frame;
    if (d->type == MOQ_MEDIA_TYPE_VIDEO) {
        if (f->width <= 0 || f->height <= 0 ||
            (size_t)f->width > SIZE_MAX / (size_t)f->height)
            return -1;
        size_t bytes = (size_t)f->width * f->height;
        if (bytes > d->gray_cap) {
            uint8_t *next = realloc(d->gray, bytes);
            if (!next) return -1;
            d->gray = next;
            d->gray_cap = bytes;
        }
        d->scale = sws_getCachedContext(d->scale, f->width, f->height,
                    (enum AVPixelFormat)f->format, f->width, f->height,
                    AV_PIX_FMT_GRAY8, SWS_FAST_BILINEAR, NULL, NULL, NULL);
        if (!d->scale) return -1;
        uint8_t *dst[4] = {d->gray, NULL, NULL, NULL};
        int stride[4] = {f->width, 0, 0, 0};
        if (sws_scale(d->scale, (const uint8_t * const *)f->data,
                      f->linesize, 0, f->height, dst, stride) <= 0)
            return -1;
        if (d->video) d->video(d->ctx, d->gray, f->width, f->height, f->width);
        return 0;
    }

    if (d->type != MOQ_MEDIA_TYPE_AUDIO || !d->audio) return 0;
    if (f->sample_rate <= 0 || f->ch_layout.nb_channels <= 0) return -1;
    AVChannelLayout mono = AV_CHANNEL_LAYOUT_MONO;
    if (!d->resample && swr_alloc_set_opts2(&d->resample, &mono,
            AV_SAMPLE_FMT_S16, 48000, &f->ch_layout,
            (enum AVSampleFormat)f->format, f->sample_rate, 0, NULL) < 0)
        return -1;
    if (!d->resample ||
        (!swr_is_initialized(d->resample) && swr_init(d->resample) < 0))
        return -1;
    int64_t delay = swr_get_delay(d->resample, f->sample_rate);
    int64_t capacity = av_rescale_rnd(delay + f->nb_samples, 48000,
                                      f->sample_rate, AV_ROUND_UP);
    if (capacity <= 0 || capacity > INT_MAX ||
        (size_t)capacity > SIZE_MAX / sizeof(int16_t)) return -1;
    if ((size_t)capacity > d->pcm_cap) {
        int16_t *next = realloc(d->pcm, (size_t)capacity * sizeof(int16_t));
        if (!next) return -1;
        d->pcm = next;
        d->pcm_cap = (size_t)capacity;
    }
    uint8_t *output[1] = {(uint8_t *)d->pcm};
    int n = swr_convert(d->resample, output, (int)capacity,
                        (const uint8_t **)f->extended_data, f->nb_samples);
    if (n < 0) return -1;
    if (n > 0) d->audio(d->ctx, d->pcm, (size_t)n);
    return 0;
}

moqy_decoder_t *moqy_decoder_create(const moq_media_track_desc_t *desc,
                                    moqy_video_fn video, moqy_audio_fn audio,
                                    void *ctx)
{
    if (!desc || desc->init.has_cenc) return NULL;
    enum AVCodecID id = codec_id(desc->init.codec_kind);
    if (id == AV_CODEC_ID_NONE) id = codec_id_from_name(desc->codec);
    if (id == AV_CODEC_ID_NONE) return NULL;
    const AVCodec *codec = avcodec_find_decoder(id);
    if (!codec) return NULL;
    moqy_decoder_t *d = calloc(1, sizeof(*d));
    if (!d) return NULL;
    d->codec = avcodec_alloc_context3(codec);
    d->frame = av_frame_alloc();
    d->video = video;
    d->audio = audio;
    d->ctx = ctx;
    d->type = desc->info.media_type;
    if (!d->codec || !d->frame) goto fail;
    moq_bytes_t config = desc->init.codec_config;
    if (config.len > INT_MAX || config.len > SIZE_MAX - AV_INPUT_BUFFER_PADDING_SIZE ||
        (config.len && !config.data))
        goto fail;
    if (config.len) {
        d->codec->extradata = av_mallocz(config.len + AV_INPUT_BUFFER_PADDING_SIZE);
        if (!d->codec->extradata) goto fail;
        memcpy(d->codec->extradata, config.data, config.len);
        d->codec->extradata_size = (int)config.len;
    }
    if (avcodec_open2(d->codec, codec, NULL) < 0) goto fail;
    return d;
fail:
    moqy_decoder_destroy(d);
    return NULL;
}

static int feed_packet(moqy_decoder_t *d, const uint8_t *data, size_t len,
                       int64_t pts, bool keyframe)
{
    if (!data || !len || len > INT_MAX) return -1;
    AVPacket *packet = av_packet_alloc();
    if (!packet) return -1;
    int rc = -1;
    if (av_new_packet(packet, (int)len) < 0) goto done;
    memcpy(packet->data, data, len);
    packet->pts = pts;
    packet->dts = pts;
    if (keyframe) packet->flags |= AV_PKT_FLAG_KEY;
    if (avcodec_send_packet(d->codec, packet) < 0) goto done;
    rc = 0;
    while (avcodec_receive_frame(d->codec, d->frame) == 0) {
        if (handle_frame(d) < 0) rc = -1;
        av_frame_unref(d->frame);
    }
done:
    av_packet_free(&packet);
    return rc;
}

int moqy_decoder_feed(moqy_decoder_t *d, const moq_media_object_t *obj)
{
    if (!d || !obj || obj->status != MOQ_OBJECT_NORMAL) return -1;
    if (obj->packaging != MOQ_MEDIA_PACKAGING_CMAF)
        return feed_packet(d, obj->payload.data, obj->payload.len,
                           (int64_t)obj->presentation_time_us, obj->keyframe);
    if (!obj->fragment.data || obj->mdat_offset > obj->fragment.len ||
        obj->mdat_len > obj->fragment.len - obj->mdat_offset ||
        !obj->samples || !obj->sample_count)
        return -1;
    const uint8_t *mdat = obj->fragment.data + obj->mdat_offset;
    size_t offset = 0;
    for (size_t i = 0; i < obj->sample_count; ++i) {
        size_t size = obj->samples[i].size;
        if (!size || size > obj->mdat_len - offset) return -1;
        if (feed_packet(d, mdat + offset, size,
                        (int64_t)obj->presentation_time_us,
                        i == 0 && obj->keyframe) < 0)
            return -1;
        offset += size;
    }
    return offset == obj->mdat_len ? 0 : -1;
}

void moqy_decoder_destroy(moqy_decoder_t *d)
{
    if (!d) return;
    avcodec_free_context(&d->codec);
    av_frame_free(&d->frame);
    sws_freeContext(d->scale);
    swr_free(&d->resample);
    free(d->gray);
    free(d->pcm);
    free(d);
}

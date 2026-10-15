#define _POSIX_C_SOURCE 200809L

#include "decode.h"
#include "render.h"

#include <moq/endpoint.h>
#include <moq/media_receiver.h>

#include <SDL.h>

#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

enum { MAX_TRACKS = 32, WAVE_ROWS = 5, WAVE_SAMPLES = 4096 };

typedef struct track {
    moq_media_track_t *handle;
    moqy_decoder_t *decoder;
    bool warned;
} track_t;

typedef struct app {
    moq_endpoint_t *endpoint;
    moq_media_receiver_t *receiver;
    track_t tracks[MAX_TRACKS];
    size_t track_count;
    SDL_AudioDeviceID speaker;
    bool waveform;
    bool terminal_active;
    uint8_t *gray;
    size_t gray_capacity;
    int frame_width;
    int frame_height;
    int16_t recent[WAVE_SAMPLES];
    size_t recent_count;
    uint64_t last_draw_ms;
    unsigned long long frames;
    unsigned long long audio_blocks;
} app_t;

static volatile sig_atomic_t stop_requested;

static void on_signal(int signal_number)
{
    (void)signal_number;
    stop_requested = 1;
}

static uint64_t monotonic_ms(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000 + (uint64_t)now.tv_nsec / 1000000;
}

static void restore_terminal(app_t *app)
{
    if (app->terminal_active) {
        fputs("\033[?25h\033[?1049l", stdout);
        fflush(stdout);
        app->terminal_active = false;
    }
}

static void draw(app_t *app)
{
    struct winsize size;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) != 0) return;
    int columns = size.ws_col;
    int rows = size.ws_row - (app->waveform ? WAVE_ROWS : 0);
    if (columns <= 0 || rows <= 0) return;
    if (columns > 240) columns = 240;
    if (rows > 100) rows = 100;
    size_t frame_size = ((size_t)columns + 1) * rows + 1;
    size_t wave_size = ((size_t)columns + 1) * WAVE_ROWS + 1;
    char *text = malloc(frame_size + wave_size);
    if (!text) return;
    size_t used = 0;
    if (app->gray) {
        used = moqy_ascii_frame(app->gray, app->frame_width,
                 app->frame_height, app->frame_width,
                 columns, rows, text, frame_size);
    }
    if (!used) {
        memset(text, ' ', frame_size - 1);
        for (int y = 0; y < rows; ++y) text[(size_t)y * (columns + 1) + columns] = '\n';
        used = frame_size - 1;
    }
    if (app->waveform)
        used += moqy_waveform(app->recent, app->recent_count, columns,
                              text + used, wave_size);
    fputs("\033[H", stdout);
    fwrite(text, 1, used, stdout);
    fflush(stdout);
    free(text);
    app->last_draw_ms = monotonic_ms();
}

static void on_video(void *ctx, const uint8_t *gray,
                     int width, int height, int stride)
{
    app_t *app = ctx;
    if (width <= 0 || height <= 0 || (size_t)width > SIZE_MAX / (size_t)height)
        return;
    size_t bytes = (size_t)width * height;
    if (bytes > app->gray_capacity) {
        uint8_t *next = realloc(app->gray, bytes);
        if (!next) return;
        app->gray = next;
        app->gray_capacity = bytes;
    }
    for (int y = 0; y < height; ++y)
        memcpy(app->gray + (size_t)y * width, gray + (size_t)y * stride, width);
    app->frame_width = width;
    app->frame_height = height;
    app->frames++;
    draw(app);
}

static void on_audio(void *ctx, const int16_t *samples, size_t count)
{
    app_t *app = ctx;
    app->audio_blocks++;
    if (app->speaker && SDL_GetAudioDeviceStatus(app->speaker) == SDL_AUDIO_PLAYING) {
        if (SDL_GetQueuedAudioSize(app->speaker) > 48000) SDL_ClearQueuedAudio(app->speaker);
        if (count <= UINT32_MAX / sizeof(int16_t) &&
            SDL_QueueAudio(app->speaker, samples, (Uint32)(count * sizeof(int16_t))) == 0)
            return;
        SDL_CloseAudioDevice(app->speaker);
        app->speaker = 0;
        app->waveform = true;
    }
    if (count > WAVE_SAMPLES) {
        samples += count - WAVE_SAMPLES;
        count = WAVE_SAMPLES;
    }
    memcpy(app->recent, samples, count * sizeof(int16_t));
    app->recent_count = count;
    if (monotonic_ms() - app->last_draw_ms >= 100) draw(app);
}

static void open_speaker(app_t *app)
{
    if (app->speaker || app->waveform) return;
    if (SDL_Init(SDL_INIT_AUDIO) != 0 ||
        (SDL_GetCurrentAudioDriver() &&
         strcmp(SDL_GetCurrentAudioDriver(), "dummy") == 0)) {
        app->waveform = true;
        return;
    }
    SDL_AudioSpec want = {0};
    want.freq = 48000;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 1024;
    app->speaker = SDL_OpenAudioDevice(NULL, 0, &want, NULL, 0);
    if (app->speaker) SDL_PauseAudioDevice(app->speaker, 0);
    else app->waveform = true;
}

static void print_usage(const char *program)
{
    fprintf(stderr,
        "usage: %s <moqt://host:port|https://host:port/path> <namespace> "
        "[--insecure-skip-verify] [--waveform]\n"
        "  Connect with draft 22 exactly; press Ctrl-C to quit.\n"
        "  --waveform forces the five-line audio display.\n", program);
}

static int split_namespace(char *buffer, moq_bytes_t *parts)
{
    int count = 0;
    char *cursor = buffer;
    if (!*cursor) return -1;
    while (*cursor) {
        if (count == MAX_TRACKS) return -1;
        char *slash = strchr(cursor, '/');
        if (slash) *slash = '\0';
        size_t length = strlen(cursor);
        if (!length) return -1;
        parts[count].data = (const uint8_t *)cursor;
        parts[count++].len = length;
        if (!slash) break;
        cursor = slash + 1;
        if (!*cursor) return -1;
    }
    return count;
}

static track_t *find_track(app_t *app, moq_media_track_t *handle)
{
    for (size_t i = 0; i < app->track_count; ++i)
        if (app->tracks[i].handle == handle) return &app->tracks[i];
    return NULL;
}

static void poll_tracks(app_t *app)
{
    moq_media_track_event_t event;
    while (moq_media_receiver_poll_track(app->receiver, &event, sizeof(event)) == MOQ_OK) {
        if (event.kind == MOQ_MEDIA_TRACK_ADDED && event.desc &&
            app->track_count < MAX_TRACKS) {
            if (event.desc->info.media_type != MOQ_MEDIA_TYPE_VIDEO &&
                event.desc->info.media_type != MOQ_MEDIA_TYPE_AUDIO)
                continue;
            track_t *track = &app->tracks[app->track_count++];
            track->handle = event.track;
            track->decoder = moqy_decoder_create(event.desc, on_video, on_audio, app);
            if (!track->decoder) {
                fprintf(stderr, "unsupported or encrypted media track\n");
                continue;
            }
            if (event.desc->info.media_type == MOQ_MEDIA_TYPE_AUDIO)
                open_speaker(app);
            moq_result_t rc = moq_media_receiver_subscribe_track(app->receiver,
                                                                  event.track, NULL);
            if (rc != MOQ_OK) {
                fprintf(stderr, "track subscribe failed: %s\n", moq_strerror(rc));
                moqy_decoder_destroy(track->decoder);
                track->decoder = NULL;
            }
        } else if (event.kind == MOQ_MEDIA_TRACK_REMOVED ||
                   event.kind == MOQ_MEDIA_TRACK_ENDED) {
            track_t *track = find_track(app, event.track);
            if (track) {
                moqy_decoder_destroy(track->decoder);
                track->decoder = NULL;
            }
        } else if (event.kind == MOQ_MEDIA_TRACK_PARSE_DROP) {
            fprintf(stderr, "media object dropped by receiver\n");
        }
    }
}

static int run(app_t *app, const char *url, moq_bytes_t *parts, int part_count,
               bool insecure)
{
    moq_endpoint_cfg_t config;
    moq_endpoint_cfg_init_sized(&config, sizeof(config));
    config.url.data = (const uint8_t *)url;
    config.url.len = strlen(url);
    config.insecure_skip_verify = insecure;
    static const moq_version_t draft22 = MOQ_VERSION_DRAFT_22;
    config.versions.struct_size = sizeof(config.versions);
    config.versions.policy = MOQ_VERSION_POLICY_EXACT;
    config.versions.versions = &draft22;
    config.versions.version_count = 1;
    moq_result_t rc = moq_endpoint_connect(&config, &app->endpoint);
    if (rc != MOQ_OK) {
        fprintf(stderr, "connect failed: %s\n", moq_strerror(rc));
        return 1;
    }

    moq_media_receiver_cfg_t receive;
    moq_media_receiver_cfg_init_live(&receive);
    receive.namespace_.parts = parts;
    receive.namespace_.count = (size_t)part_count;
    receive.auto_subscribe = false;
    rc = moq_media_receiver_attach(app->endpoint, &receive, &app->receiver);
    if (rc != MOQ_OK) {
        fprintf(stderr, "receiver attach failed: %s\n", moq_strerror(rc));
        return 1;
    }
    fputs("\033[?1049h\033[?25l\033[2J", stdout);
    fflush(stdout);
    app->terminal_active = true;
    draw(app);
    while (!stop_requested) {
        poll_tracks(app);
        moq_media_object_t object;
        rc = moq_media_receiver_poll_object(app->receiver, &object, sizeof(object));
        if (rc == MOQ_OK) {
            track_t *track = find_track(app, object.track);
            if (track && track->decoder &&
                moqy_decoder_feed(track->decoder, &object) < 0 && !track->warned) {
                fprintf(stderr, "media decode failed for a track\n");
                track->warned = true;
            }
            moq_media_object_cleanup(&object);
            continue;
        }
        if (rc == MOQ_ERR_CLOSED || rc == MOQ_ERR_INTERRUPTED) break;
        rc = moq_media_receiver_wait(app->receiver, 100000);
        if (rc == MOQ_ERR_CLOSED || rc == MOQ_ERR_INTERRUPTED) break;
    }
    bool fatal = moq_media_receiver_is_fatal(app->receiver);
    restore_terminal(app);
    if (fatal) fprintf(stderr, "receiver failed (code %llu)\n",
                       (unsigned long long)moq_media_receiver_fatal_code(app->receiver));
    fprintf(stderr, "displayed %llu frames, decoded %llu audio blocks\n",
            app->frames, app->audio_blocks);
    return fatal ? 1 : 0;
}

int main(int argc, char **argv)
{
    if (argc == 2 && (!strcmp(argv[1], "--help") || !strcmp(argv[1], "-h"))) {
        print_usage(argv[0]);
        return 0;
    }
    if (argc < 3) { print_usage(argv[0]); return 2; }
    const char *url = argv[1];
    if (strncmp(url, "moqt://", 7) && strncmp(url, "https://", 8)) {
        print_usage(argv[0]);
        return 2;
    }
    char *namespace_copy = strdup(argv[2]);
    if (!namespace_copy) return 1;
    moq_bytes_t parts[MAX_TRACKS];
    int part_count = split_namespace(namespace_copy, parts);
    if (part_count < 0) {
        fprintf(stderr, "invalid namespace\n");
        free(namespace_copy);
        return 2;
    }
    bool insecure = false;
    app_t app = {0};
    for (int i = 3; i < argc; ++i) {
        if (!strcmp(argv[i], "--insecure-skip-verify")) insecure = true;
        else if (!strcmp(argv[i], "--waveform")) app.waveform = true;
        else {
            print_usage(argv[0]);
            free(namespace_copy);
            return 2;
        }
    }
    if (!isatty(STDOUT_FILENO)) {
        fprintf(stderr, "a terminal on stdout is required\n");
        free(namespace_copy);
        return 2;
    }
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    int status = run(&app, url, parts, part_count, insecure);
    restore_terminal(&app);
    for (size_t i = 0; i < app.track_count; ++i)
        moqy_decoder_destroy(app.tracks[i].decoder);
    if (app.receiver) moq_media_receiver_destroy(app.receiver);
    if (app.endpoint) {
        moq_endpoint_stop(app.endpoint);
        moq_endpoint_destroy(app.endpoint);
    }
    if (app.speaker) SDL_CloseAudioDevice(app.speaker);
    SDL_Quit();
    free(app.gray);
    free(namespace_copy);
    return status;
}

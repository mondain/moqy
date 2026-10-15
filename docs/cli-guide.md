# moqy CLI guide

This guide covers a Linux source build and a complete draft-22 publish and
subscribe example. `moqy` is a subscriber: it needs a relay and a publisher
that offers a compatible media catalog. Its video output requires a terminal.

## 1. Get the build dependencies

Install a C11 compiler, CMake 3.20 or newer, Git, pkg-config, FFmpeg
development libraries, SDL2 development files, and the dependencies needed
by picoquic and picotls. For Debian or Ubuntu, the starting package set is:

```sh
sudo apt update
sudo apt install build-essential cmake git pkg-config libssl-dev \
  libavcodec-dev libavutil-dev libswscale-dev libswresample-dev \
  libsdl2-dev ffmpeg unzip curl
```

Keep compatible `moq5`, picoquic, and picotls source trees alongside `moqy`.
The managed QUIC and WebTransport clients in moq5 require the picoquic source
tree. If your checkouts are elsewhere, pass their absolute paths to CMake.

```sh
mkdir moq-work
cd moq-work
git clone https://github.com/mondain/moqy.git
git clone https://github.com/openmoq/moq5.git
git clone https://github.com/private-octopus/picoquic.git
git clone https://github.com/h2o/picotls.git
git -C moq5 checkout c600ead6167d3070b26daaa97f5a6d31d001dc2a
git -C picoquic checkout b4075dd184f4ae3276e4347308ffc7b0cfac737f
git -C picotls checkout f06553b877ada93c860dd1db9002eabf7365292d
cmake -S picotls -B picotls/build
cmake --build picotls/build -j
```

These are the moq5, picoquic, and picotls revisions used for the local build
and relay test. A different `moq5` revision must still define
`MOQ_VERSION_DRAFT_22`, and picoquic and picotls must be a compatible pair.

## 2. Build and check moqy

From `moq-work`:

```sh
cmake -S moqy -B moqy/build \
  -DMOQ5_SOURCE_DIR="$PWD/moq5" \
  -DMOQ_PICOQUIC_SOURCE_DIR="$PWD/picoquic" \
  -DMOQ_PICOTLS_PREFIX="$PWD/picotls/build"
cmake --build moqy/build --target moqy -j
ctest --test-dir moqy/build --output-on-failure
moqy/build/moqy --help
```

The executable is `moqy/build/moqy`; no installation step is needed.

## 3. Connect to a publisher

The syntax is:

```text
moqy <moqt://host:port|https://host:port/path> <namespace> [--waveform] [--insecure-skip-verify]
```

For native MoQT over QUIC, run this in an interactive terminal:

```sh
moqy/build/moqy moqt://moq-relay.red5.net:4433 demo/bbb
```

For a relay that exposes WebTransport at `/moq`, use its HTTPS URL:

```sh
moqy/build/moqy https://relay.example:4433/moq live/camera --waveform
```

The relay URL and namespace are separate arguments. The namespace must match
the publisher exactly, including every slash-separated component. Empty
components such as `live//camera` are rejected. `moqy` negotiates draft 22
exactly, so both the relay and publisher must support it. Start the subscriber
while the publisher is still active. Press Ctrl-C to stop; `moqy` restores the
terminal and prints decoded video and audio counts to standard error.

| Option | Effect |
| --- | --- |
| `--waveform` | Show the five-line audio waveform instead of opening a speaker. |
| `--insecure-skip-verify` | Skip certificate verification for a trusted local test relay with a self-signed certificate. |
| `--help` or `-h` | Show CLI syntax. |

Without `--waveform`, `moqy` tries the default SDL audio output device. It
switches to the waveform if no usable device opens or playback fails. Audio
is decoded to mono 48 kHz PCM. The waveform occupies the bottom five terminal
rows, leaving the remaining rows for video. A terminal of at least 80 columns
and 24 rows is a useful starting size. Standard output must be a TTY; piping
it to a file is not supported.

## 4. Try a public video with moqxr

The repository's GIF was captured from a 16-second section of
[Big Buck Bunny](https://download.blender.org/peach/bigbuckbunny_movies/BigBuckBunny_320x180.mp4.zip).
Download the public 320×180 file and make a fragmented MP4. These commands
create the sample in the current directory:

```sh
curl -L -o bbb.zip \
  https://download.blender.org/peach/bigbuckbunny_movies/BigBuckBunny_320x180.mp4.zip
unzip bbb.zip
ffmpeg -ss 90 -i BigBuckBunny_320x180.mp4 -t 16 \
  -map 0:v:0 -map 0:a:0 -vf fps=10,scale=160:90 \
  -c:v libx264 -preset fast -crf 24 -g 20 -keyint_min 20 \
  -sc_threshold 0 -bf 0 -pix_fmt yuv420p \
  -c:a aac -b:a 64k -ar 48000 -ac 1 \
  -movflags +frag_keyframe+empty_moov+default_base_moof+separate_moof \
  -f mp4 bbb-fragmented.mp4
```

Clone [moqxr](https://github.com/openmoq/moqxr) alongside `moqy` with
`git clone https://github.com/openmoq/moqxr.git`, then build its
`openmoq-publisher` executable using the moqxr README. From `moq-work`,
publish the clip with draft 22 in one terminal. `--loop` keeps this short
sample available long enough to start the subscriber:

```sh
moqxr/build/openmoq-publisher \
  --input bbb-fragmented.mp4 --paced --loop \
  --endpoint moqt://moq-relay.red5.net:4433 \
  --namespace demo/bbb --draft 22 --publish-catalog --forward 0 \
  --timeout 120
```

If your checkout is elsewhere, replace the executable path. In a second
terminal, run `moqy` from `moq-work` with the same relay and namespace:

```sh
moqy/build/moqy moqt://moq-relay.red5.net:4433 demo/bbb --waveform
```

Start the publisher first, then start the subscriber promptly. A successful
transport connection by itself does not prove media delivery: check that the
terminal picture changes and that the final frame and audio counts increase.
The public relay is an example and may be unavailable or have different
policies later; you can use any compatible draft-22 relay.

Big Buck Bunny excerpt: (c) copyright 2008, Blender Foundation /
www.bigbuckbunny.org, licensed under
[Creative Commons Attribution 3.0](https://peach.blender.org/about/).

## Troubleshooting

| Symptom | Check |
| --- | --- |
| CMake cannot find moq5 or draft 22 | Check `MOQ5_SOURCE_DIR` and confirm that checkout defines `MOQ_VERSION_DRAFT_22`. |
| picoquic or picotls configuration fails | Use a picoquic source tree and a compatible built picotls checkout; verify `MOQ_PICOQUIC_SOURCE_DIR` and `MOQ_PICOTLS_PREFIX`. |
| FFmpeg or SDL2 is missing | Install the development packages, then rerun CMake. The runtime `ffmpeg` command alone is not enough to compile `moqy`. |
| `a terminal on stdout is required` | Run `moqy` directly in an interactive terminal. |
| The screen stays blank | Confirm relay URL, exact namespace, draft 22, a published media catalog, and a publisher still sending media. |
| `unsupported or encrypted media track` | Use a clear track encoded with a decoder supported by your local FFmpeg build. |
| `media decode failed for a track` | Check the publisher's codec and packaging. Multi-chunk CMAF objects are outside the current decoder's scope. |
| No speaker audio | Use `--waveform` to check whether audio blocks arrive, then check the system's default SDL output device. |
| TLS connection fails on a local test relay | Verify its certificate or use `--insecure-skip-verify` only when you trust that relay. |

The current client supports one namespace at a time. It recognizes clear
H.264, HEVC, AV1, AAC, and Opus when FFmpeg provides the corresponding
decoder. Raw/LOC packets and single-`mdat` CMAF objects are supported;
encrypted media and more complex CMAF layouts are not.

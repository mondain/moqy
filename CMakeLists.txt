cmake_minimum_required(VERSION 3.20)
project(moqy LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(MOQ5_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../moq5" CACHE PATH
    "Path to a moq5 source checkout with draft-22 support")
if(NOT EXISTS "${MOQ5_SOURCE_DIR}/service/include/moq/media_receiver.h")
    message(FATAL_ERROR "MOQ5_SOURCE_DIR must point to a moq5 source checkout")
endif()
file(STRINGS "${MOQ5_SOURCE_DIR}/core/include/moq/session.h" _draft22
     REGEX "MOQ_VERSION_DRAFT_22[ \\t]*=[ \\t]*22")
if(NOT _draft22)
    message(FATAL_ERROR "MOQ5_SOURCE_DIR must support draft 22; select the draft-22 moq5 branch")
endif()

set(MOQ_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(MOQ_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(MOQ_BUILD_SIM OFF CACHE BOOL "" FORCE)
set(MOQ_BUILD_MSF ON CACHE BOOL "" FORCE)
set(MOQ_BUILD_SERVICE ON CACHE BOOL "" FORCE)
set(MOQ_BUILD_ADAPTER_PICOQUIC ON CACHE BOOL "" FORCE)
set(MOQ_BUILD_PQ_THREADED ON CACHE BOOL "" FORCE)
set(MOQ_BUILD_ADAPTER_PICO_WT ON CACHE BOOL "" FORCE)
set(MOQ_BUILD_PICO_WT_MANAGED ON CACHE BOOL "" FORCE)
add_subdirectory("${MOQ5_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/moq5")

find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
    libavcodec libavutil libswscale libswresample)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)

add_executable(moqy src/main.c src/decode.c src/render.c)
target_include_directories(moqy PRIVATE src)
target_link_libraries(moqy PRIVATE moq::service PkgConfig::FFMPEG PkgConfig::SDL2)
target_compile_options(moqy PRIVATE -Wall -Wextra -Wpedantic)

enable_testing()
add_executable(test_render tests/test_render.c src/render.c)
target_include_directories(test_render PRIVATE src)
target_compile_options(test_render PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
add_test(NAME render COMMAND test_render)

add_executable(test_decode tests/test_decode.c src/decode.c)
target_include_directories(test_decode PRIVATE src)
target_link_libraries(test_decode PRIVATE moq::service PkgConfig::FFMPEG)
target_compile_options(test_decode PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
add_test(NAME decode COMMAND test_decode)

find_program(FFMPEG_EXECUTABLE ffmpeg)
if(FFMPEG_EXECUTABLE)
    set(VIDEO_FIXTURE "${CMAKE_CURRENT_BINARY_DIR}/frame.h264")
    add_custom_command(OUTPUT "${VIDEO_FIXTURE}"
        COMMAND "${FFMPEG_EXECUTABLE}" -hide_banner -loglevel error
                -f lavfi -i color=c=white:s=16x16:r=1 -frames:v 1
                -c:v libx264 -preset ultrafast -tune zerolatency
                -f h264 -y "${VIDEO_FIXTURE}"
        VERBATIM)
    add_custom_target(video_fixture DEPENDS "${VIDEO_FIXTURE}")
    add_executable(test_video tests/test_video.c src/decode.c)
    target_include_directories(test_video PRIVATE src)
    target_link_libraries(test_video PRIVATE moq::service PkgConfig::FFMPEG)
    target_compile_options(test_video PRIVATE -UNDEBUG)
    add_dependencies(test_video video_fixture)
    add_test(NAME video_decode COMMAND test_video "${VIDEO_FIXTURE}")

    set(AUDIO_FIXTURE "${CMAKE_CURRENT_BINARY_DIR}/tone.aac")
    add_custom_command(OUTPUT "${AUDIO_FIXTURE}"
        COMMAND "${FFMPEG_EXECUTABLE}" -hide_banner -loglevel error
                -f lavfi -i sine=frequency=1000:sample_rate=48000:duration=0.1
                -frames:a 1 -c:a aac -f adts -y "${AUDIO_FIXTURE}"
        VERBATIM)
    add_custom_target(audio_fixture DEPENDS "${AUDIO_FIXTURE}")
    add_executable(test_audio tests/test_audio.c src/decode.c)
    target_include_directories(test_audio PRIVATE src)
    target_link_libraries(test_audio PRIVATE moq::service PkgConfig::FFMPEG)
    target_compile_options(test_audio PRIVATE -UNDEBUG)
    add_dependencies(test_audio audio_fixture)
    add_test(NAME audio_decode COMMAND test_audio "${AUDIO_FIXTURE}")

    set(CMAF_FIXTURE "${CMAKE_CURRENT_BINARY_DIR}/frame.mp4")
    add_custom_command(OUTPUT "${CMAF_FIXTURE}"
        COMMAND "${FFMPEG_EXECUTABLE}" -hide_banner -loglevel error
                -f lavfi -i color=c=white:s=16x16:r=1 -frames:v 1
                -c:v libx264 -preset ultrafast -tune zerolatency
                -movflags +empty_moov+frag_keyframe+default_base_moof
                -f mp4 -y "${CMAF_FIXTURE}"
        VERBATIM)
    add_custom_target(cmaf_fixture DEPENDS "${CMAF_FIXTURE}")
    add_executable(test_cmaf_video tests/test_cmaf_video.c src/decode.c)
    target_include_directories(test_cmaf_video PRIVATE src)
    target_link_libraries(test_cmaf_video PRIVATE moq::service moq::cmaf PkgConfig::FFMPEG)
    target_compile_options(test_cmaf_video PRIVATE -UNDEBUG)
    add_dependencies(test_cmaf_video cmaf_fixture)
    add_test(NAME cmaf_video_decode COMMAND test_cmaf_video "${CMAF_FIXTURE}")
endif()

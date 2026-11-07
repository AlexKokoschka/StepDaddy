#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p tests/build
ccflags='-std=c11 -fshort-enums -O1 -g -Wall -Wextra -Wno-unused-parameter -fsanitize=undefined -fno-sanitize-recover=all -DMERGED_HOST_TEST -Itests/support -Isrc -Ivoice/src'
gcc $ccflags -c tests/integration.c -o tests/build/integration.o
gcc $ccflags -c src/persist.c -o tests/build/persist.o
gcc $ccflags -c src/profile.c -o tests/build/profile.o
gcc $ccflags -c src/telemetry.c -o tests/build/telemetry.o
gcc $ccflags -c tests/flash_backend.c -o tests/build/flash_backend.o
gcc $ccflags -c src/merged.c -o tests/build/merged.o
gcc $ccflags -c tests/yummy_under_test.c -o tests/build/ui.o
gcc $ccflags -c voice/src/voice303.c -o tests/build/voice.o
gcc $ccflags -c voice/src/seq303.c -o tests/build/seq.o
g++ -std=c++17 -fshort-enums -O1 -g -Wall -Wextra -Wno-unused-parameter -fsanitize=undefined -fno-sanitize-recover=all -Isrc -c tests/bro_under_test.cpp -o tests/build/bro.o
g++ -fsanitize=undefined tests/build/integration.o tests/build/persist.o tests/build/profile.o tests/build/telemetry.o tests/build/flash_backend.o tests/build/merged.o tests/build/ui.o tests/build/voice.o tests/build/seq.o tests/build/bro.o -lm -o tests/build/integration
tests/build/integration
gcc $ccflags tests/journal.c src/persist.c -o tests/build/journal
tests/build/journal
python3 tests/test_decoder.py
gcc $ccflags tests/profile.c src/profile.c -o tests/build/profile_test
tests/build/profile_test
refnames='-Dvoice_init=ref_voice_init -Dvoice_set_params=ref_voice_set_params -Dvoice_set_tempo=ref_voice_set_tempo -Dvoice_chord_on=ref_voice_chord_on -Dvoice_note_on=ref_voice_note_on -Dvoice_note_off=ref_voice_note_off -Dvoice_render=ref_voice_render -Dvoice_delay_div=ref_voice_delay_div -Dvoice_delay_div_names=ref_voice_delay_div_names'
gcc $ccflags $refnames -c tests/reference_voice303.c -o tests/build/reference_voice.o
gcc $ccflags tests/delay.c voice/src/voice303.c tests/build/reference_voice.o -lm -o tests/build/delay_test
tests/build/delay_test
gcc $ccflags -c src/persist.c -o tests/build/file_persist.o
g++ -std=c++17 -O1 -g -Wall -Wextra -Wno-unused-parameter -fsanitize=undefined -fno-sanitize-recover=all -Itests/platform -Isrc -Ivoice/src tests/storage_platform.cpp src/storage_teensy.cpp tests/build/file_persist.o -o tests/build/storage_platform
tests/build/storage_platform
gcc $ccflags tests/audio_platform.c src/audio.c src/profile.c voice/src/voice303.c voice/src/seq303.c -lm -o tests/build/audio_platform
tests/build/audio_platform
make -C voice test profile-test stress-test
L=vendor/avr/libraries/LittleFS/src/littlefs
gcc -std=c11 -O1 -g -I$L -DLFS_NO_DEBUG -DLFS_NO_WARN -DLFS_NO_ERROR tests/lfs_capacity.c $L/lfs.c $L/lfs_util.c -o tests/build/lfs_capacity 2>/dev/null
tests/build/lfs_capacity

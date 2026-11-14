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
python3 tests/test_led_init.py
gcc $ccflags tests/profile.c src/profile.c -o tests/build/profile_test
tests/build/profile_test
gcc $ccflags tests/delay.c -lm -o tests/build/delay_test
tests/build/delay_test
make -C voice test profile-test stress-test

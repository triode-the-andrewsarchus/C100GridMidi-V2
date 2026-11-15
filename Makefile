CC ?= cc
SEQUENCE_CC ?= 74
GRID_ROTATION ?= 180
CPPFLAGS += -DACID_SEQUENCE_CC=$(SEQUENCE_CC)
CC_CONFIG = .build/sequence-cc
CFLAGS ?= -O2 -g -std=c11 -Wall -Wextra -Werror -Wpedantic
CORE = core/grid.c core/acid.c core/phrase.c core/learning.c core/acid_patch.c
KEYMAP = keyboards/keychron/c100_8k/keymaps/grid_logic
QMK_HOME ?= ../qmk_firmware
.PHONY: test test-sanitize firmware simulator clean
.build:
	mkdir -p $@
.PHONY: FORCE
$(CC_CONFIG): FORCE | .build
	@if ! test -r "$@" || test "$$(cat "$@")" != "$(SEQUENCE_CC)"; then printf '%s\n' "$(SEQUENCE_CC)" > "$@"; fi
.build/test_grid: tests/test_acid.c $(CORE) $(CC_CONFIG) core/grid.h core/acid.h | .build
	$(CC) $(CPPFLAGS) $(CFLAGS) -Icore tests/test_acid.c $(CORE) -o $@
test: .build/test_grid
	./.build/test_grid
test-sanitize:
	$(MAKE) test test-ties test-json test-firmware test-duplex test-ui test-synth test-scaling CFLAGS="-O1 -g -std=c11 -Wall -Wextra -Werror -Wpedantic -fsanitize=address,undefined -fno-omit-frame-pointer" -B
firmware:
	$(MAKE) -C "$(QMK_HOME)" keychron/c100_8k:grid_logic QMK_USERSPACE="$(CURDIR)" SEQUENCE_CC="$(SEQUENCE_CC)" GRID_ROTATION="$(GRID_ROTATION)"
clean:
	rm -rf .build

SIM_FLAGS = $(shell pkg-config --cflags sdl2 cairo json-c alsa)
SIM_LIBS = $(shell pkg-config --libs sdl2 cairo json-c alsa) -pthread -lm
SIM_SRC = simulator/main.c simulator/runtime.c simulator/patch_json.c simulator/synth.c
.build/grid-simulator: $(SIM_SRC) simulator/runtime.h simulator/synth.h simulator/patch_json.h $(CORE) $(CC_CONFIG) core/grid.h core/acid.h | .build
	$(CC) $(CPPFLAGS) $(CFLAGS) -Icore $(SIM_FLAGS) $(SIM_SRC) $(CORE) $(SIM_LIBS) -o $@
simulator: .build/grid-simulator

.build/test_json: tests/test_json.c simulator/patch_json.c $(CORE) $(CC_CONFIG) core/grid.h core/acid.h | .build
	$(CC) $(CPPFLAGS) $(CFLAGS) -Icore -Isimulator $(shell pkg-config --cflags json-c) tests/test_json.c simulator/patch_json.c $(CORE) $(shell pkg-config --libs json-c) -o $@
.build/test_duplex: tests/test_duplex.c simulator/runtime.c simulator/synth.c simulator/synth.h $(CORE) $(CC_CONFIG) core/grid.h core/acid.h | .build
	$(CC) $(CPPFLAGS) $(CFLAGS) -Icore -Isimulator tests/test_duplex.c simulator/runtime.c simulator/synth.c $(CORE) $(shell pkg-config --libs alsa) -pthread -lm -o $@
.PHONY: test-json test-duplex
test-json: .build/test_json
	./.build/test_json
test-duplex: .build/test_duplex
	./.build/test_duplex || test $$? -eq 77

# Check the actual QMK descriptor resolver plus the C100 speed adapter.
.PHONY: test-usb
test-usb: firmware
	$(CC) $(CPPFLAGS) $(filter-out -Wpedantic,$(CFLAGS)) -Wno-unused-parameter -fshort-wchar \
	  -I"$(QMK_HOME)" -I"$(QMK_HOME)/tmk_core/protocol/chibios/lufa_utils" \
	  -I"$(QMK_HOME)/platforms" -I"$(QMK_HOME)/tmk_core/protocol" \
	  -I"$(QMK_HOME)/quantum" -Itests/usb -I$(KEYMAP) \
	  -I"$(QMK_HOME)/tmk_core/protocol/chibios" \
	  -I"$(QMK_HOME)/lib/chibios-contrib/os/hal/ports/AT32/LLD/OTGv1" \
	  -include "$(QMK_HOME)/.build/obj_keychron_c100_8k_grid_logic/src/info_config.h" \
	  -DMAX_ENDPOINTS=15 -DMIDI_ENABLE -DNKRO_ENABLE -DSHARED_EP_ENABLE \
	  -DMOUSE_ENABLE -DMOUSE_SHARED_EP -DEXTRAKEY_ENABLE \
	  -DFIXED_CONTROL_ENDPOINT_SIZE=64 -DFIXED_NUM_CONFIGURATIONS=1 \
	  "$(QMK_HOME)/tmk_core/protocol/usb_descriptor.c" \
	  $(KEYMAP)/usb_descriptor_midi.c $(KEYMAP)/usb_midi_transport.c \
	  tests/test_usb_descriptors.c tests/usb/runtime.c -Wl,--wrap=usbStart -o .build/test_usb
	./.build/test_usb

.build/test_firmware_%: tests/test_firmware.c tests/firmware/qmk_keyboard.h $(KEYMAP)/keymap.c $(KEYMAP)/grid_usb.c $(CORE) $(CC_CONFIG) core/grid.h core/acid.h | .build
	$(CC) $(CPPFLAGS) $(CFLAGS) -Icore -Itests/firmware -I$(KEYMAP) -DGRID_ROTATION=$* -DQMK_KEYBOARD_H='"qmk_keyboard.h"' tests/test_firmware.c $(KEYMAP)/grid_usb.c $(CORE) -o $@
.PHONY: test-firmware
test-firmware: $(addprefix .build/test_firmware_,0 90 180 270)
	@for angle in 0 90 180 270; do ./.build/test_firmware_$$angle || exit $$?; done

.PHONY: check
check: test test-ties test-json test-firmware test-duplex test-ui test-synth test-scaling simulator

.build/test_simulator_ui: tests/test_simulator_ui.c $(SIM_SRC) simulator/runtime.h simulator/synth.h $(CORE) $(CC_CONFIG) core/grid.h core/acid.h | .build
	$(CC) $(CPPFLAGS) $(CFLAGS) -Icore $(SIM_FLAGS) tests/test_simulator_ui.c simulator/runtime.c simulator/patch_json.c simulator/synth.c $(CORE) $(SIM_LIBS) $(shell pkg-config --libs fontconfig) -o $@
.PHONY: test-ui
test-ui: .build/test_simulator_ui
	SDL_VIDEODRIVER=dummy ./.build/test_simulator_ui

.build/test_synth: tests/test_synth.c simulator/synth.c simulator/synth.h $(CORE) $(CC_CONFIG) core/grid.h core/acid.h | .build
	$(CC) $(CPPFLAGS) $(CFLAGS) -Icore -Isimulator tests/test_synth.c simulator/synth.c $(CORE) -lm -o $@
.PHONY: test-synth
test-synth: .build/test_synth
	./.build/test_synth

.build/test_scaling: tests/test_scaling.c $(SIM_SRC) simulator/runtime.h simulator/synth.h $(CORE) $(CC_CONFIG) core/grid.h core/acid.h | .build
	$(CC) $(CPPFLAGS) $(CFLAGS) -Icore $(SIM_FLAGS) tests/test_scaling.c simulator/runtime.c simulator/patch_json.c simulator/synth.c $(CORE) $(SIM_LIBS) -o $@
.PHONY: test-scaling
test-scaling: .build/test_scaling
	SDL_VIDEODRIVER=dummy ./.build/test_scaling

.build/test_ties: tests/test_ties.c $(CORE) $(CC_CONFIG) core/grid.h core/acid.h | .build
	$(CC) $(CPPFLAGS) $(CFLAGS) -Icore tests/test_ties.c $(CORE) -o $@
.PHONY: test-ties
test-ties: .build/test_ties
	./.build/test_ties

MIDI_ENABLE = yes
VIA_ENABLE = no
RGB_MATRIX_CUSTOM_USER = yes
SRC += usb_descriptor_midi.c usb_midi_transport.c grid_usb.c
SRC += $(QMK_USERSPACE)/core/grid.c $(QMK_USERSPACE)/core/acid.c
SRC += $(QMK_USERSPACE)/core/phrase.c $(QMK_USERSPACE)/core/learning.c
SRC += $(QMK_USERSPACE)/core/acid_patch.c
EXTRAINCDIRS += $(QMK_USERSPACE)/core
LDFLAGS += -Wl,--wrap=usbStart
# An upstream Pixel Rain state is unused with recent GCC.
CFLAGS += -Wno-error=unused-but-set-variable

# Phrase generation and validated bank decoding need more than QMK's 2 KiB default.
USE_PROCESS_STACKSIZE = 0x1000

SEQUENCE_CC ?= 74
OPT_DEFS += -DACID_SEQUENCE_CC=$(SEQUENCE_CC)

# Physical clockwise rotation from the factory orientation.
GRID_ROTATION ?= 180
OPT_DEFS += -DGRID_ROTATION=$(GRID_ROTATION)

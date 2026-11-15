// SPDX-License-Identifier: GPL-2.0-or-later
#include "patch_json.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
int main(void) {
    Grid g; grid_init(&g, 578);
    char path[120]; snprintf(path, sizeof(path), "/tmp/c100-acid-%ld.json", (long)getpid());
    Patch decoded; uint8_t before[ACID_PATCH_BYTES], after[ACID_PATCH_BYTES];
    g.acid.patch.patterns[5].pitch_lock = true; g.acid.patch.patterns[4].octave[7] = -1;
    g.acid.patch.patterns[0].steps[0][14] = GATE_LONG;
    g.acid.patch.patterns[0].gate_priority = 14;
    assert(patch_json_save(&g.acid.patch, path)); assert(patch_json_load(&decoded, path));
    grid_patch_encode(&g.acid.patch, before); grid_patch_encode(&decoded, after);
    assert(!memcmp(before, after, sizeof(before)));
    FILE *f = fopen(path, "w"); assert(f); fputs("{\"format\":\"old-spatial\",\"bank\":\"00\"}", f); fclose(f);
    assert(!patch_json_load(&decoded, path));
    assert(unlink(path) == 0);
    puts("ok atomic JSON eight-pattern bank roundtrip and invalid format rejection");
}

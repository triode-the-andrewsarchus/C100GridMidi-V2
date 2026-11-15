// SPDX-License-Identifier: GPL-2.0-or-later
#define _POSIX_C_SOURCE 200809L
#include "patch_json.h"
#include <json-c/json.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
bool patch_json_save(const Patch *p, const char *path) {
    if (!acid_patch_valid(p)) return false;
    uint8_t bytes[ACID_PATCH_BYTES]; char hex[ACID_PATCH_BYTES * 2 + 1];
    grid_patch_encode(p, bytes);
    for (unsigned i = 0; i < sizeof(bytes); ++i) snprintf(hex + i * 2, 3, "%02x", bytes[i]);
    json_object *root = json_object_new_object();
    json_object_object_add(root, "format", json_object_new_string("C100 phrase-memory bank v3"));
    json_object_object_add(root, "bank", json_object_new_string(hex));
    size_t size = strlen(path) + 12;
    char *temporary = malloc(size);
    if (!temporary) {
        json_object_put(root);
        return false;
    }
    snprintf(temporary, size, "%s.XXXXXX", path);
    int fd = mkstemp(temporary);
    bool ok = false;
    if (fd >= 0) {
        const char *text = json_object_to_json_string_ext(root, JSON_C_TO_STRING_PRETTY);
        size_t length = strlen(text);
        ok = write(fd, text, length) == (ssize_t)length;
        if (ok)
            ok = fsync(fd) == 0;
        if (close(fd) < 0)
            ok = false;
        if (ok)
            ok = rename(temporary, path) == 0;
        if (!ok)
            unlink(temporary);
    }
    free(temporary);
    json_object_put(root);
    return ok;
}
static int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}
static bool read_bank(json_object *root, Patch *p) {
    json_object *bank, *format;
    if (!json_object_object_get_ex(root, "format", &format) ||
        !json_object_is_type(format, json_type_string) ||
        (strcmp(json_object_get_string(format), "C100 phrase-memory bank v1") &&
         strcmp(json_object_get_string(format), "C100 phrase-memory bank v2") &&
         strcmp(json_object_get_string(format), "C100 phrase-memory bank v3"))) return false;
    if (!json_object_object_get_ex(root, "bank", &bank) ||
        !json_object_is_type(bank, json_type_string) ||
        json_object_get_string_len(bank) != ACID_PATCH_BYTES * 2) return false;
    const char *hex = json_object_get_string(bank); uint8_t bytes[ACID_PATCH_BYTES];
    for (unsigned i = 0; i < sizeof(bytes); ++i) {
        int hi = hex_digit(hex[i * 2]), lo = hex_digit(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) return false;
        bytes[i] = hi * 16 + lo;
    }
    return grid_patch_decode(p, bytes, sizeof(bytes));
}
bool patch_json_load(Patch *p, const char *path) {
    json_object *root = json_object_from_file(path);
    if (!root) return false;
    bool ok = read_bank(root, p); json_object_put(root); return ok;
}

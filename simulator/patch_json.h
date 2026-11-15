// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "grid.h"
bool patch_json_save(const Patch *patch, const char *path);
bool patch_json_load(Patch *patch, const char *path);

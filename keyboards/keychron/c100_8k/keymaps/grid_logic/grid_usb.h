// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "grid.h"
void grid_usb_init(Grid *grid);
void grid_usb_drain(Grid *grid);
bool grid_usb_ready(void);

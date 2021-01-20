/*
 * Copyright (c) 2021 Bastien Nocera <hadess@hadess.net>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 3 as published by
 * the Free Software Foundation.
 *
 */

#pragma once

#include <glib.h>

#define IS_TEST (g_getenv ("UMOCKDEV_DIR") != NULL)

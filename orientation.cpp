/*
 * Copyright (c) 2011, 2014 Bastien Nocera <hadess@hadess.net>
 *
 * orientation_calc() from the sensorfw package
 * Copyright (C) 2009-2010 Nokia Corporation
 * Authors:
 *   Üstün Ergenoglu <ext-ustun.ergenoglu@nokia.com>
 *   Timo Rongas <ext-timo.2.rongas@nokia.com>
 *   Lihan Guo <lihan.guo@digia.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 3 as published by
 * the Free Software Foundation.
 *
 */

#include <stdint.h>
#include <stdlib.h>
#include <glib.h>

#include "orientation.h"

static const char *orientations[] = {
        "undefined",
        "normal",
        "bottom-up",
        "left-up",
        "right-up",
        NULL
};

const char *
orientation_to_string (OrientationUp o)
{
        return orientations[o];
}

OrientationUp
string_to_orientation (const char *orientation)
{
        int i;

        if (orientation == NULL)
                return ORIENTATION_UNDEFINED;
        for (i = 0; orientations[i] != NULL; i++) {
                if (g_str_equal (orientation, orientations[i]))
                        return (OrientationUp) i;
        }
        return ORIENTATION_UNDEFINED;
}

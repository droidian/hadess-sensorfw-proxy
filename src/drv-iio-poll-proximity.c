/*
 * Copyright (c) 2019 Purism SPC
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 3 as published by
 * the Free Software Foundation.
 */

#include "drivers.h"
#include "iio-buffer-utils.h"

#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#define PROXIMITY_NEAR_LEVEL "PROXIMITY_NEAR_LEVEL"

#define PROXIMITY_WATER_MARK_LOW  0.9
#define PROXIMITY_WATER_MARK_HIGH 1.1

typedef struct DrvData {
	guint               timeout_id;
	ReadingsUpdateFunc  callback_func;
	gpointer            user_data;
	GUdevDevice        *dev;
	const char         *name;
	gint                near_level;
	gint                last_level;
} DrvData;

static DrvData *drv_data = NULL;

static gboolean
poll_proximity (gpointer user_data)
{
	DrvData *data = user_data;
	ProximityReadings readings;
	gint prox;
	gdouble near_level = data->near_level;

	/* g_udev_device_get_sysfs_attr_as_int does not update when there's no event */
	prox = g_udev_device_get_sysfs_attr_as_int_uncached (data->dev, "in_proximity_raw");
	/* Use a margin so we don't trigger too often */
	near_level *=  (data->last_level > near_level) ? PROXIMITY_WATER_MARK_LOW : PROXIMITY_WATER_MARK_HIGH;
	readings.is_near = (prox > near_level) ? PROXIMITY_NEAR_TRUE : PROXIMITY_NEAR_FALSE;
	g_debug ("Proximity read from IIO on '%s': %d/%f, near: %d", data->name, prox, near_level, readings.is_near);
	data->last_level = prox;

	drv_data->callback_func (&iio_poll_proximity, (gpointer) &readings, drv_data->user_data);

	return G_SOURCE_CONTINUE;
}

static gboolean
iio_poll_proximity_discover (GUdevDevice *device)
{
	return drv_check_udev_sensor_type (device, "iio-poll-proximity", "IIO poll proximity sensor");
}

static void
iio_poll_proximity_set_polling (gboolean state)
{
	if (drv_data->timeout_id > 0 && state)
		return;
	if (drv_data->timeout_id == 0 && !state)
		return;

	g_clear_handle_id (&drv_data->timeout_id, g_source_remove);
	if (state) {
		drv_data->timeout_id = g_timeout_add (700, poll_proximity, drv_data);
		g_source_set_name_by_id (drv_data->timeout_id, "[iio_poll_proximity_set_polling] poll_proximity");
	}
}

static gint
get_near_level (GUdevDevice *device)
{
	gint near_level;

	near_level = g_udev_device_get_property_as_int (device, PROXIMITY_NEAR_LEVEL);
	if (!near_level)
		near_level = g_udev_device_get_sysfs_attr_as_int (device, "in_proximity_nearlevel");

	if (!near_level) {
		g_warning ("Found proximity sensor but no " PROXIMITY_NEAR_LEVEL " udev property");
		g_warning ("See https://gitlab.freedesktop.org/hadess/iio-sensor-proxy/blob/master/README.md");
		return 0;
	}

	g_debug ("Near level: %d", near_level);
	return near_level;
}


static gboolean
iio_poll_proximity_open (GUdevDevice        *device,
			 ReadingsUpdateFunc  callback_func,
			 gpointer	     user_data)
{
	iio_fixup_sampling_frequency (device);

	drv_data = g_new0 (DrvData, 1);
	drv_data->dev = g_object_ref (device);
	drv_data->name = g_udev_device_get_sysfs_attr (device, "name");
	drv_data->callback_func = callback_func;
	drv_data->user_data = user_data;
	drv_data->near_level = get_near_level (device);

	if (!drv_data->near_level) {
		g_free (drv_data);
		return FALSE;
	}

	return TRUE;
}

static void
iio_poll_proximity_close (void)
{
	g_clear_object (&drv_data->dev);
	g_clear_pointer (&drv_data, g_free);
}

SensorDriver iio_poll_proximity = {
	.name = "IIO Poll proximity sensor",
	.type = DRIVER_TYPE_PROXIMITY,

	.discover = iio_poll_proximity_discover,
	.open = iio_poll_proximity_open,
	.set_polling = iio_poll_proximity_set_polling,
	.close = iio_poll_proximity_close,
};

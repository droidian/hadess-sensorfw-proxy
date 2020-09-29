/*
 * Copyright (c) 2014 Bastien Nocera <hadess@hadess.net>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 3 as published by
 * the Free Software Foundation.
 */

#include "drivers.h"
#include "iio-buffer-utils.h"
#include "accel-mount-matrix.h"

#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct DrvData {
	guint               timeout_id;
	ReadingsUpdateFunc  callback_func;
	gpointer            user_data;
	GUdevDevice        *dev;
	const char         *name;
	AccelVec3          *mount_matrix;
	AccelLocation       location;
	AccelScale          scale;
} DrvData;

static DrvData *drv_data = NULL;

static gboolean
poll_orientation (gpointer user_data)
{
	DrvData *data = user_data;
	int accel_x, accel_y, accel_z;
	AccelReadings readings;
	AccelVec3 tmp;

	accel_x = g_udev_device_get_sysfs_attr_as_int_uncached (data->dev, "in_accel_x_raw");
	accel_y = g_udev_device_get_sysfs_attr_as_int_uncached (data->dev, "in_accel_y_raw");
	accel_z = g_udev_device_get_sysfs_attr_as_int_uncached (data->dev, "in_accel_z_raw");
	copy_accel_scale (&readings.scale, data->scale);

	g_debug ("Accel read from IIO on '%s': %d, %d, %d (scale %lf,%lf,%lf)", data->name,
		 accel_x, accel_y, accel_z,
		 data->scale.x, data->scale.y, data->scale.z);

	tmp.x = accel_x;
	tmp.y = accel_y;
	tmp.z = accel_z;

	if (!apply_mount_matrix (drv_data->mount_matrix, &tmp))
		g_warning ("Could not apply mount matrix");

	//FIXME report errors
	readings.accel_x = tmp.x;
	readings.accel_y = tmp.y;
	readings.accel_z = tmp.z;

	drv_data->callback_func (&iio_poll_accel, (gpointer) &readings, drv_data->user_data);

	return G_SOURCE_CONTINUE;
}

static gboolean
iio_poll_accel_discover (GUdevDevice *device)
{
	/* We also handle devices with trigger buffers, but there's no trigger available on the system */
	if (!drv_check_udev_sensor_type (device, "iio-poll-accel", NULL) &&
	    !drv_check_udev_sensor_type (device, "iio-buffer-accel", NULL))
		return FALSE;

	g_debug ("Found IIO poll accelerometer at %s", g_udev_device_get_sysfs_path (device));
	return TRUE;
}

static void
iio_poll_accel_set_polling (gboolean state)
{
	if (drv_data->timeout_id > 0 && state)
		return;
	if (drv_data->timeout_id == 0 && !state)
		return;

	if (drv_data->timeout_id) {
		g_source_remove (drv_data->timeout_id);
		drv_data->timeout_id = 0;
	}

	if (state) {
		drv_data->timeout_id = g_timeout_add (700, poll_orientation, drv_data);
		g_source_set_name_by_id (drv_data->timeout_id, "[iio_poll_accel_set_polling] poll_orientation");
	}
}

static gboolean
iio_poll_accel_open (GUdevDevice        *device,
		     ReadingsUpdateFunc  callback_func,
		     gpointer            user_data)
{
	iio_fixup_sampling_frequency (device);

	drv_data = g_new0 (DrvData, 1);
	drv_data->dev = g_object_ref (device);
	drv_data->name = g_udev_device_get_sysfs_attr (device, "name");
	drv_data->mount_matrix = setup_mount_matrix (device);
	drv_data->location = setup_accel_location (device);
	drv_data->callback_func = callback_func;
	drv_data->user_data = user_data;
	if (!get_accel_scale (device, &drv_data->scale))
		reset_accel_scale (&drv_data->scale);

	return TRUE;
}

static void
iio_poll_accel_close (void)
{
	iio_poll_accel_set_polling (FALSE);
	g_clear_object (&drv_data->dev);
	g_clear_pointer (&drv_data->mount_matrix, g_free);
	g_clear_pointer (&drv_data, g_free);
}

SensorDriver iio_poll_accel = {
	.name = "IIO Poll accelerometer",
	.type = DRIVER_TYPE_ACCEL,
	.specific_type = DRIVER_TYPE_ACCEL_IIO,

	.discover = iio_poll_accel_discover,
	.open = iio_poll_accel_open,
	.set_polling = iio_poll_accel_set_polling,
	.close = iio_poll_accel_close,
};

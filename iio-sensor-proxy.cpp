/*
 * Copyright (c) 2014-2020 Bastien Nocera <hadess@hadess.net>
 *                         Erfan Abdi <erfangplus@gmail.com>
 * 
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 3 as published by
 * the Free Software Foundation.
 *
 */

#include <iostream>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <gio/gio.h>
#include <gudev/gudev.h>

#include "orientation.h"
#include "iio-sensor-proxy-resources.h"

#include "sensorfw-core/console_log.h"
#include "sensorfw-core/sensorfw_proximity_sensor.h"
#include "sensorfw-core/sensorfw_light_sensor.h"
#include "sensorfw-core/sensorfw_orientation_sensor.h"
#include "sensorfw-core/sensorfw_compass_sensor.h"

#define SENSOR_PROXY_DBUS_NAME          "net.hadess.SensorProxy"
#define SENSOR_PROXY_DBUS_PATH          "/net/hadess/SensorProxy"
#define SENSOR_PROXY_COMPASS_DBUS_PATH  "/net/hadess/SensorProxy/Compass"
#define SENSOR_PROXY_IFACE_NAME         SENSOR_PROXY_DBUS_NAME
#define SENSOR_PROXY_COMPASS_IFACE_NAME SENSOR_PROXY_DBUS_NAME ".Compass"

#define NUM_SENSOR_TYPES DRIVER_TYPE_PROXIMITY + 1

typedef enum {
	DRIVER_TYPE_ACCEL,
	DRIVER_TYPE_LIGHT,
	DRIVER_TYPE_COMPASS,
	DRIVER_TYPE_PROXIMITY,
} DriverType;

typedef struct {
	GMainLoop *loop;
	GUdevClient *client;
	GDBusNodeInfo *introspection_data;
	GDBusConnection *connection;
	guint name_id;
	int ret;

	GHashTable   *clients[NUM_SENSOR_TYPES]; /* key = D-Bus name, value = watch ID */

	/* Orientation */
	OrientationUp previous_orientation;
	gboolean accel_avaliable;
	std::shared_ptr<repowerd::OrientationSensor> orientation_sensor;

	/* Light */
	gdouble previous_level;
	gboolean uses_lux;
	gboolean light_avaliable;
	std::shared_ptr<repowerd::LightSensor> light_sensor;

	/* Compass */
	gdouble previous_heading;
	gboolean compass_avaliable;
	std::shared_ptr<repowerd::CompassSensor> compass_sensor;

	/* Proximity */
	gboolean previous_prox_near;
	gboolean prox_avaliable;
	std::shared_ptr<repowerd::ProximitySensor> proximity_sensor;
} SensorData;

static const char *
driver_type_to_str (DriverType type)
{
	switch (type) {
	case DRIVER_TYPE_ACCEL:
		return "accelerometer";
	case DRIVER_TYPE_LIGHT:
		return "ambient light sensor";
	case DRIVER_TYPE_COMPASS:
		return "compass";
	case DRIVER_TYPE_PROXIMITY:
		return "proximity";
	default:
		g_assert_not_reached ();
	}
}

static gboolean
driver_type_exists (SensorData *data,
		    DriverType  driver_type)
{
	switch (driver_type) {
	case DRIVER_TYPE_ACCEL:
		return (data->accel_avaliable == TRUE);
	case DRIVER_TYPE_LIGHT:
		return (data->light_avaliable == TRUE);
	case DRIVER_TYPE_COMPASS:
		return (data->compass_avaliable == TRUE);
	case DRIVER_TYPE_PROXIMITY:
		return (data->prox_avaliable == TRUE);
	default:
		return FALSE;
	}
	return FALSE;
}

static void
free_client_watch (gpointer data)
{
	guint watch_id = GPOINTER_TO_UINT (data);

	if (watch_id == 0)
		return;
	g_bus_unwatch_name (watch_id);
}

static GHashTable *
create_clients_hash_table (void)
{
	return g_hash_table_new_full (g_str_hash, g_str_equal,
				      g_free, free_client_watch);
}

enum {
	PROP_HAS_ACCELEROMETER		= 1 << 0,
	PROP_ACCELEROMETER_ORIENTATION  = 1 << 1,
	PROP_HAS_AMBIENT_LIGHT		= 1 << 2,
	PROP_LIGHT_LEVEL		= 1 << 3,
	PROP_HAS_COMPASS                = 1 << 4,
	PROP_COMPASS_HEADING            = 1 << 5,
	PROP_HAS_PROXIMITY              = 1 << 6,
	PROP_PROXIMITY_NEAR             = 1 << 7,
};

#define PROP_ALL (PROP_HAS_ACCELEROMETER | \
                  PROP_ACCELEROMETER_ORIENTATION | \
                  PROP_HAS_AMBIENT_LIGHT | \
                  PROP_LIGHT_LEVEL | \
                  PROP_HAS_PROXIMITY | \
		  PROP_PROXIMITY_NEAR)
#define PROP_ALL_COMPASS (PROP_HAS_COMPASS | \
			  PROP_COMPASS_HEADING)

static void
send_dbus_event (SensorData     *data,
		 int  mask)
{
	GVariantBuilder props_builder;
	GVariant *props_changed = NULL;

	if (data->connection == NULL)
		return;

	if (mask == 0)
		return;

	g_assert ((mask & PROP_ALL) == 0 || (mask & PROP_ALL_COMPASS) == 0);

	g_variant_builder_init (&props_builder, G_VARIANT_TYPE ("a{sv}"));

	if (mask & PROP_HAS_ACCELEROMETER) {
		gboolean has_accel;

		has_accel = driver_type_exists (data, DRIVER_TYPE_ACCEL);
		g_variant_builder_add (&props_builder, "{sv}", "HasAccelerometer",
				       g_variant_new_boolean (has_accel));

		/* Send the orientation when the device appears */
		if (has_accel)
			mask |= PROP_ACCELEROMETER_ORIENTATION;
		else
			data->previous_orientation = ORIENTATION_UNDEFINED;
	}

	if (mask & PROP_ACCELEROMETER_ORIENTATION) {
		g_variant_builder_add (&props_builder, "{sv}", "AccelerometerOrientation",
				       g_variant_new_string (orientation_to_string (data->previous_orientation)));
	}

	if (mask & PROP_HAS_AMBIENT_LIGHT) {
		gboolean has_als;

		has_als = driver_type_exists (data, DRIVER_TYPE_LIGHT);
		g_variant_builder_add (&props_builder, "{sv}", "HasAmbientLight",
				       g_variant_new_boolean (has_als));

		/* Send the light level when the device appears */
		if (has_als)
			mask |= PROP_LIGHT_LEVEL;
	}

	if (mask & PROP_LIGHT_LEVEL) {
		g_variant_builder_add (&props_builder, "{sv}", "LightLevelUnit",
				       g_variant_new_string (data->uses_lux ? "lux" : "vendor"));
		g_variant_builder_add (&props_builder, "{sv}", "LightLevel",
				       g_variant_new_double (data->previous_level));
	}

	if (mask & PROP_HAS_COMPASS) {
		gboolean has_compass;

		has_compass = driver_type_exists (data, DRIVER_TYPE_COMPASS);
		g_variant_builder_add (&props_builder, "{sv}", "HasCompass",
				       g_variant_new_boolean (has_compass));

		/* Send the heading when the device appears */
		if (has_compass)
			mask |= PROP_COMPASS_HEADING;
	}

	if (mask & PROP_COMPASS_HEADING) {
		g_variant_builder_add (&props_builder, "{sv}", "CompassHeading",
				       g_variant_new_double (data->previous_heading));
	}

	if (mask & PROP_HAS_PROXIMITY) {
		gboolean has_proximity;

		has_proximity = driver_type_exists (data, DRIVER_TYPE_PROXIMITY);
		g_variant_builder_add (&props_builder, "{sv}", "HasProximity",
				       g_variant_new_boolean (has_proximity));

		/* Send proximity information when the device appears */
		if (has_proximity)
			mask |= PROP_PROXIMITY_NEAR;
	}

	if (mask & PROP_PROXIMITY_NEAR) {
		g_variant_builder_add (&props_builder, "{sv}", "ProximityNear",
				       g_variant_new_boolean (data->previous_prox_near));
	}

	props_changed = g_variant_new ("(s@a{sv}@as)", (mask & PROP_ALL) ? SENSOR_PROXY_IFACE_NAME : SENSOR_PROXY_COMPASS_IFACE_NAME,
				       g_variant_builder_end (&props_builder),
				       g_variant_new_strv (NULL, 0));

	g_dbus_connection_emit_signal (data->connection,
				       NULL,
				       (mask & PROP_ALL) ? SENSOR_PROXY_DBUS_PATH : SENSOR_PROXY_COMPASS_DBUS_PATH,
				       "org.freedesktop.DBus.Properties",
				       "PropertiesChanged",
				       props_changed, NULL);
}

static void
client_release (SensorData            *data,
		const char            *sender,
		DriverType             driver_type)
{
	GHashTable *ht;
	guint watch_id;

	ht = data->clients[driver_type];

	watch_id = GPOINTER_TO_UINT (g_hash_table_lookup (ht, sender));
	if (watch_id == 0)
		return;

	g_hash_table_remove (ht, sender);
}

static void
client_vanished_cb (GDBusConnection *connection,
		    const gchar     *name,
		    gpointer         user_data)
{
	SensorData *data = (SensorData *) user_data;
	guint i;
	char *sender;

	if (name == NULL)
		return;

	sender = g_strdup (name);

	for (i = 0; i < NUM_SENSOR_TYPES; i++) {
		GHashTable *ht;
		guint watch_id;

		ht = data->clients[i];
		g_assert (ht);

		watch_id = GPOINTER_TO_UINT (g_hash_table_lookup (ht, sender));
		if (watch_id > 0)
			client_release(data, sender, (DriverType) i);
	}

	g_free (sender);
}

static void
handle_generic_method_call (SensorData            *data,
			    const gchar           *sender,
			    const gchar           *object_path,
			    const gchar           *interface_name,
			    const gchar           *method_name,
			    GVariant              *parameters,
			    GDBusMethodInvocation *invocation,
			    DriverType             driver_type)
{
	GHashTable *ht;
	guint watch_id;

	g_debug ("Handling driver refcounting method '%s' for %s device",
		 method_name, driver_type_to_str (driver_type));

	ht = data->clients[driver_type];

	if (g_str_has_prefix (method_name, "Claim")) {
		watch_id = GPOINTER_TO_UINT (g_hash_table_lookup (ht, sender));
		if (watch_id > 0) {
			g_dbus_method_invocation_return_value (invocation, NULL);
			return;
		}

		watch_id = g_bus_watch_name_on_connection (data->connection,
							   sender,
							   G_BUS_NAME_WATCHER_FLAGS_NONE,
							   NULL,
							   client_vanished_cb,
							   data,
							   NULL);
		g_hash_table_insert (ht, g_strdup (sender), GUINT_TO_POINTER (watch_id));

		g_dbus_method_invocation_return_value (invocation, NULL);
	} else if (g_str_has_prefix (method_name, "Release")) {
		client_release (data, sender, driver_type);
		g_dbus_method_invocation_return_value (invocation, NULL);
	}
}

static void
handle_method_call (GDBusConnection       *connection,
		    const gchar           *sender,
		    const gchar           *object_path,
		    const gchar           *interface_name,
		    const gchar           *method_name,
		    GVariant              *parameters,
		    GDBusMethodInvocation *invocation,
		    gpointer               user_data)
{
	SensorData *data = (SensorData *) user_data;
	DriverType driver_type;

	if (g_strcmp0 (method_name, "ClaimAccelerometer") == 0 ||
	    g_strcmp0 (method_name, "ReleaseAccelerometer") == 0)
		driver_type = DRIVER_TYPE_ACCEL;
	else if (g_strcmp0 (method_name, "ClaimLight") == 0 ||
		 g_strcmp0 (method_name, "ReleaseLight") == 0)
		driver_type = DRIVER_TYPE_LIGHT;
	else if (g_strcmp0 (method_name, "ClaimProximity") == 0 ||
		 g_strcmp0 (method_name, "ReleaseProximity") == 0)
	        driver_type = DRIVER_TYPE_PROXIMITY;
	else {
		g_dbus_method_invocation_return_error (invocation,
						       G_DBUS_ERROR,
						       G_DBUS_ERROR_UNKNOWN_METHOD,
						       "Method '%s' does not exist on object %s",
						       method_name, object_path);
		return;
	}

	handle_generic_method_call (data, sender, object_path,
				    interface_name, method_name,
				    parameters, invocation, driver_type);
}

static GVariant *
handle_get_property (GDBusConnection *connection,
		     const gchar     *sender,
		     const gchar     *object_path,
		     const gchar     *interface_name,
		     const gchar     *property_name,
		     GError         **error,
		     gpointer         user_data)
{
	SensorData *data = (SensorData *) user_data;

	if (data->connection == NULL)
		return NULL;

	if (g_strcmp0 (property_name, "HasAccelerometer") == 0)
		return g_variant_new_boolean (driver_type_exists (data, DRIVER_TYPE_ACCEL));
	if (g_strcmp0 (property_name, "AccelerometerOrientation") == 0)
		return g_variant_new_string (orientation_to_string (data->previous_orientation));
	if (g_strcmp0 (property_name, "HasAmbientLight") == 0)
		return g_variant_new_boolean (driver_type_exists (data, DRIVER_TYPE_LIGHT));
	if (g_strcmp0 (property_name, "LightLevelUnit") == 0)
		return g_variant_new_string (data->uses_lux ? "lux" : "vendor");
	if (g_strcmp0 (property_name, "LightLevel") == 0)
		return g_variant_new_double (data->previous_level);
	if (g_strcmp0 (property_name, "HasProximity") == 0)
		return g_variant_new_boolean (driver_type_exists (data, DRIVER_TYPE_PROXIMITY));
	if (g_strcmp0 (property_name, "ProximityNear") == 0)
		return g_variant_new_boolean (data->previous_prox_near);

	return NULL;
}

static const GDBusInterfaceVTable interface_vtable =
{
	handle_method_call,
	handle_get_property,
	NULL
};

static void
handle_compass_method_call (GDBusConnection       *connection,
			    const gchar           *sender,
			    const gchar           *object_path,
			    const gchar           *interface_name,
			    const gchar           *method_name,
			    GVariant              *parameters,
			    GDBusMethodInvocation *invocation,
			    gpointer               user_data)
{
	SensorData *data = (SensorData *) user_data;

	if (g_strcmp0 (method_name, "ClaimCompass") != 0 &&
	    g_strcmp0 (method_name, "ReleaseCompass") != 0) {
		g_dbus_method_invocation_return_error (invocation,
						       G_DBUS_ERROR,
						       G_DBUS_ERROR_UNKNOWN_METHOD,
						       "Method '%s' does not exist on object %s",
						       method_name, object_path);
		return;
	}

	handle_generic_method_call (data, sender, object_path,
				    interface_name, method_name,
				    parameters, invocation, DRIVER_TYPE_COMPASS);
}

static GVariant *
handle_compass_get_property (GDBusConnection *connection,
			     const gchar     *sender,
			     const gchar     *object_path,
			     const gchar     *interface_name,
			     const gchar     *property_name,
			     GError         **error,
			     gpointer         user_data)
{
	SensorData *data = (SensorData *) user_data;

	if (data->connection == NULL)
		return NULL;

	if (g_strcmp0 (property_name, "HasCompass") == 0)
		return g_variant_new_boolean (driver_type_exists (data, DRIVER_TYPE_COMPASS));
	if (g_strcmp0 (property_name, "CompassHeading") == 0)
		return g_variant_new_double (data->previous_heading);

	return NULL;
}

static const GDBusInterfaceVTable compass_interface_vtable =
{
	handle_compass_method_call,
	handle_compass_get_property,
	NULL
};

static void
name_lost_handler (GDBusConnection *connection,
		   const gchar     *name,
		   gpointer         user_data)
{
	g_debug ("iio-sensor-proxy is already running, or it cannot own its D-Bus name. Verify installation.");
	exit (0);
}

static void
bus_acquired_handler (GDBusConnection *connection,
		      const gchar     *name,
		      gpointer         user_data)
{
	SensorData *data = (SensorData *)user_data;

	g_dbus_connection_register_object (connection,
					   SENSOR_PROXY_DBUS_PATH,
					   data->introspection_data->interfaces[0],
					   &interface_vtable,
					   data,
					   NULL,
					   NULL);

	g_dbus_connection_register_object(connection,
					   SENSOR_PROXY_COMPASS_DBUS_PATH,
					   data->introspection_data->interfaces[1],
					   &compass_interface_vtable,
					   data,
					   NULL,
					   NULL);

	data->connection = (GDBusConnection *) g_object_ref(connection);
}

char const *const log_tag = "main";

std::string the_dbus_bus_address()
{
	auto const address = std::unique_ptr<gchar, decltype(&g_free)>{
		g_dbus_address_get_for_bus_sync(G_BUS_TYPE_SYSTEM, nullptr, nullptr),
		g_free};

	return address ? address.get() : std::string{};
}

static void
name_acquired_handler (GDBusConnection *connection,
		       const gchar     *name,
		       gpointer         user_data)
{
	SensorData *data = (SensorData *)user_data;
	guint i;

	for (i = 0; i < NUM_SENSOR_TYPES; i++) {
		data->clients[i] = create_clients_hash_table ();
	}

	send_dbus_event (data, PROP_ALL);
	return;

bail:
	data->ret = 0;
	g_debug ("No sensors or missing kernel drivers for the sensors");
	g_main_loop_quit (data->loop);
}

static gboolean
setup_dbus (SensorData *data)
{
	GBytes *bytes;

	bytes = g_resources_lookup_data ("/net/hadess/SensorProxy/net.hadess.SensorProxy.xml",
					 G_RESOURCE_LOOKUP_FLAGS_NONE,
					 NULL);
	data->introspection_data = g_dbus_node_info_new_for_xml((const gchar *)g_bytes_get_data(bytes, NULL), NULL);
	g_bytes_unref (bytes);
	g_assert (data->introspection_data != NULL);

	data->name_id = g_bus_own_name (G_BUS_TYPE_SYSTEM,
					SENSOR_PROXY_DBUS_NAME,
					G_BUS_NAME_OWNER_FLAGS_NONE,
					bus_acquired_handler,
					name_acquired_handler,
					name_lost_handler,
					data,
					NULL);

	return TRUE;
}

static void
free_sensor_data (SensorData *data)
{
	guint i;

	if (data == NULL)
		return;

	if (data->name_id != 0) {
		g_bus_unown_name (data->name_id);
		data->name_id = 0;
	}

	for (i = 0; i < NUM_SENSOR_TYPES; i++) {
		g_clear_pointer (&data->clients[i], g_hash_table_unref);
	}

	g_clear_pointer (&data->introspection_data, g_dbus_node_info_unref);
	g_clear_object (&data->connection);
	g_clear_object (&data->client);
	g_clear_pointer (&data->loop, g_main_loop_unref);
	g_free (data);
}

static void
setup_sensors (SensorData *data)
{
	auto const log = std::make_shared<repowerd::ConsoleLog>();

	try
	{
		data->proximity_sensor = std::make_shared<repowerd::SensorfwProximitySensor>(log,
			the_dbus_bus_address());
		data->prox_avaliable = TRUE;
		send_dbus_event(data, PROP_HAS_PROXIMITY);
	}
	catch (std::exception const &e)
	{
		log->log(log_tag, "Failed to create SensorfwProximitySensor: %s", e.what());
		data->prox_avaliable = FALSE;
	}

	try
	{
		data->light_sensor = std::make_shared<repowerd::SensorfwLightSensor>(log,
			the_dbus_bus_address());
		data->light_avaliable = TRUE;
		send_dbus_event(data, PROP_HAS_AMBIENT_LIGHT);
	}
	catch (std::exception const &e)
	{
		log->log(log_tag, "Failed to create SensorfwLightSensor: %s", e.what());
		data->light_avaliable = FALSE;
	}

	try
	{
		data->orientation_sensor = std::make_shared<repowerd::SensorfwOrientationSensor>(log,
			the_dbus_bus_address());
		data->accel_avaliable = TRUE;
		send_dbus_event(data, PROP_HAS_ACCELEROMETER);
	}
	catch (std::exception const &e)
	{
		log->log(log_tag, "Failed to create SensorfwOrientationSensor: %s", e.what());
		data->accel_avaliable = FALSE;
	}

	try
	{
		data->compass_sensor = std::make_shared<repowerd::SensorfwCompassSensor>(log,
			the_dbus_bus_address());
		data->compass_avaliable = TRUE;
		send_dbus_event(data, PROP_HAS_COMPASS);
	}
	catch (std::exception const &e)
	{
		log->log(log_tag, "Failed to create SensorfwCompassSensor: %s", e.what());
		data->compass_avaliable = FALSE;
	}
}

int main (int argc, char **argv)
{
	SensorData *data;
	int ret = 0;

	data = g_new0 (SensorData, 1);
	data->previous_orientation = ORIENTATION_UNDEFINED;
	data->uses_lux = TRUE;

	/* Set up D-Bus */
	setup_dbus (data);

	setup_sensors(data);
	repowerd::HandlerRegistration prox_registration;
	repowerd::HandlerRegistration light_registration;
	repowerd::HandlerRegistration orientation_registration;
	repowerd::HandlerRegistration compass_registration;
	if (data->prox_avaliable == TRUE) {
		prox_registration = data->proximity_sensor->register_proximity_handler(
			[data](repowerd::ProximityState state) {
				data->previous_prox_near = (state == repowerd::ProximityState::near);
				send_dbus_event(data, PROP_PROXIMITY_NEAR);
			});
		data->proximity_sensor->enable_proximity_events();
	}
	if (data->light_avaliable == TRUE) {
		light_registration = data->light_sensor->register_light_handler(
			[data](double light) {
				if (data->previous_level != light) {
					data->previous_level = light;
					send_dbus_event(data, PROP_LIGHT_LEVEL);
				}
			});
		data->light_sensor->enable_light_events();
	}
	if (data->prox_avaliable == TRUE) {
		orientation_registration = data->orientation_sensor->register_orientation_handler(
			[data](repowerd::OrientationData value) {
				OrientationUp orientation = data->previous_orientation;
				switch (value)
				{
				case repowerd::OrientationData::LeftUp:
					orientation = ORIENTATION_LEFT_UP;
					break;
				case repowerd::OrientationData::RightUp:
					orientation = ORIENTATION_RIGHT_UP;
					break;
				case repowerd::OrientationData::BottomUp:
					orientation = ORIENTATION_BOTTOM_UP;
					break;
				case repowerd::OrientationData::BottomDown:
					orientation = ORIENTATION_NORMAL;
					break;
				case repowerd::OrientationData::FaceDown:
					orientation = ORIENTATION_NORMAL;
					break;
				case repowerd::OrientationData::FaceUp:
					orientation = ORIENTATION_NORMAL;
					break;
				default:
					orientation = ORIENTATION_UNDEFINED;
					break;
				}
				if (data->previous_orientation != orientation) {
					data->previous_orientation = orientation;
					send_dbus_event(data, PROP_ACCELEROMETER_ORIENTATION);
				}
			});
		data->orientation_sensor->enable_orientation_events();
	}
	if (data->compass_avaliable == TRUE) {
		compass_registration = data->compass_sensor->register_compass_handler(
			[data](int heading) {
				if (data->previous_heading != heading) {
					data->previous_heading = heading;
					send_dbus_event(data, PROP_COMPASS_HEADING);
				}
			});
		data->compass_sensor->enable_compass_events();
	}
	data->loop = g_main_loop_new (NULL, TRUE);
	g_main_loop_run (data->loop);
	ret = data->ret;
	free_sensor_data (data);
	if (data->prox_avaliable == TRUE)
		data->proximity_sensor->disable_proximity_events();
	if (data->light_avaliable == TRUE)
		data->light_sensor->disable_light_events();
	if (data->accel_avaliable == TRUE)
		data->orientation_sensor->disable_orientation_events();
	if (data->compass_avaliable == TRUE)
		data->compass_sensor->disable_compass_events();

	return ret;
}

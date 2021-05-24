add_library(
    sensorfw-core STATIC

    sensorfw_common.cpp
    sensorfw_light_sensor.cpp
    sensorfw_proximity_sensor.cpp
    sensorfw_orientation_sensor.cpp
    sensorfw_compass_sensor.cpp

    socketreader.cpp

    console_log.cpp
    dbus_connection_handle.cpp
    dbus_event_loop.cpp
    event_loop.cpp
    handler_registration.cpp
)

target_link_libraries(sensorfw-core PUBLIC
    Qt5::Core
    Qt5::Network

    ${GIO_LDFLAGS} ${GIO_LIBRARIES}
    ${GIO_UNIX_LDFLAGS} ${GIO_UNIX_LIBRARIES}
)

target_include_directories(sensorfw-core PUBLIC
    ${GIO_INCLUDE_DIRS}
    ${GIO_UNIX_INCLUDE_DIRS}
)

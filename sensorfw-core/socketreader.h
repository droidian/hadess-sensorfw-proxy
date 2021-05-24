/**
   @file socketreader.h
   @brief SocketReader helper class for sensor interface

   <p>
   Copyright (C) 2009-2010 Nokia Corporation

   @author Timo Rongas <ext-timo.2.rongas@nokia.com>
   @author Antti Virtanen <antti.i.virtanen@nokia.com>

   This file is part of Sensord.

   Sensord is free software; you can redistribute it and/or modify
   it under the terms of the GNU Lesser General Public License
   version 2.1 as published by the Free Software Foundation.

   Sensord is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with Sensord.  If not, see <http://www.gnu.org/licenses/>.
   </p>
 */

#pragma once

#include <QObject>
#include <QLocalSocket>
#include <QVector>

class TimedData
{
public:

    /**
     * Constructor
     *
     * @param timestamp monotonic time (microsec)
     */
    TimedData(const quint64& timestamp) : timestamp_(timestamp) {}

    quint64 timestamp_;  /**< monotonic time (microsec) */
};

class TimedUnsigned : public TimedData {
public:
    /**
     * Default constructor.
     */
    TimedUnsigned() : TimedData(0), value_(0) {}

    /**
     * Constructor.
     *
     * @param timestamp timestamp as monotonic time (microsec).
     * @param value value of the measurement.
     */
    TimedUnsigned(const quint64& timestamp, unsigned value) : TimedData(timestamp), value_(value) {}

    unsigned value_; /**< Measurement value. */
};

class ProximityData : public TimedUnsigned
{
public:
    /**
     * Default constructor.
     */
    ProximityData() : TimedUnsigned(), withinProximity_(false) {}

    /**
     * Constructor
     *
     * @param timestamp timestamp as monotonic time (microsec).
     * @param value raw proximity value.
     * @param withinProximity is there an object within proximity.
     */
    ProximityData(const quint64& timestamp, unsigned int value, bool withinProximity) :
        TimedUnsigned(timestamp, value), withinProximity_(withinProximity) {}

    bool withinProximity_; /**< is an object within proximity or not */
};

/**
 * Datatype for device pose interpretation.
 */
class PoseData : public TimedData {
public:
    /**
     * Possible device postures.
     * @note The interpretation algorithm for orientation currently relies on the
     *       integer values of the enumeration. Thus changing the names for the
     *       orientation states is completely ok (for sensord, client apps may
     *       disagree). Possible new values must be appended to the list and the
     *       order of values must not be changed!
     *
     * Device side naming:
     * @verbatim
                          Top
                      ----------
                     /  NOKIA  /|
                    /-------- / |
                   //       //  /
                  //       //  /
          Left   //  Face //  /    Right
                //       //  /
               //       //  /
              /---------/  /
             /    O    /  /
            /         /  /
            ----------  /
            |_________!/
              Bottom
       @endverbatim
     */

    /**
     * Device orientation.
     */
    enum Orientation
    {
        Undefined = 0, /**< Orientation is unknown. */
        LeftUp,        /**< Device left side is up */
        RightUp,       /**< Device right side is up */
        BottomUp,      /**< Device bottom is up */
        BottomDown,    /**< Device bottom is down */
        FaceDown,      /**< Device face is down */
        FaceUp         /**< Device face is up */
    };

    PoseData::Orientation orientation_; /**< Device Orientation */

    /**
     * Constructor.
     */
    PoseData() : TimedData(0), orientation_(Undefined) {}

    /**
     * Constructor.
     * @param orientation Initial value for orientation.
     */
    PoseData(Orientation orientation) : TimedData(0), orientation_(orientation) {}

    /**
     * Constructor
     * @param timestamp Initial value for timestamp.
     * @param orientation Initial value for orientation.
     */
    PoseData(const quint64& timestamp, Orientation orientation) : TimedData(timestamp), orientation_(orientation) {}
};

/**
 * Datatype for compass measurements.
 */
class CompassData : public TimedData
{
public:
    /**
     * Default constructor.
     */
    CompassData() : TimedData(0), degrees_(0), rawDegrees_(0), correctedDegrees_(0), level_(0) {}

    /**
     * Constructor.
     *
     * @param timestamp timestamp as monotonic time (microsec).
     * @param degrees Angle to north.
     * @param level Magnetometer calibration level.
     */
    CompassData(const quint64& timestamp, int degrees, int level) :
        TimedData(timestamp), degrees_(degrees), rawDegrees_(degrees), correctedDegrees_(0), level_(level) {}

    /**
     * Constructor.
     *
     * @param timestamp timestamp as monotonic time (microsec).
     * @param degrees Angle to north.
     * @param level Magnetometer calibration level.
     * @param correctedDegrees Declination corrected angle to north.
     * @param rawDegrees Not declination corrected angle to north.
     */
    CompassData(const quint64& timestamp, int degrees, int level, int correctedDegrees, int rawDegrees) :
        TimedData(timestamp), degrees_(degrees), rawDegrees_(rawDegrees), correctedDegrees_(correctedDegrees), level_(level) {}

    int degrees_; /**< Angle to north which may be declination corrected or not. This is the value apps should use */
    int rawDegrees_; /**< Angle to north without declination correction */
    int correctedDegrees_; /**< Declination corrected angle to north */
    int level_;   /**< Magnetometer calibration level. Higher value means better calibration. */
};

/**
 * @brief Helper class for reading socket datachannel from sensord
 *
 * SocketReader provides common handler for all sensors using socket
 * data channel. It is used by AbstractSensorChannelInterface to maintain
 * the socket connection to the server.
 */
class SocketReader : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY(SocketReader)

public:

    /**
     * Constructor.
     *
     * @param parent Parent QObject.
     */
    SocketReader(QObject* parent = 0);

    /**
     * Destructor.
     */
    ~SocketReader();

    /**
     * Initiates new data socket connection.
     *
     * @param sessionId ID for the current session.
     * @return was the connection established successfully.
     */
    bool initiateConnection(int sessionId);

    /**
     * Drops socket connection.
     * @return was the connection successfully closed.
     */
    bool dropConnection();

    /**
     * Provides access to the internal QLocalSocket for direct reading.
     *
     * @return Pointer to the internal QLocalSocket. Pointer can be \c NULL
     *         if \c initiateConnection() has not been called successfully.
     */
    QLocalSocket* socket();

    /**
     * Attempt to read given number of bytes from the socket. As
     * QLocalSocket is used, we are guaranteed that any number of bytes
     * written in single operation are available for immediate reading
     * with a single operation.
     *
     * @param size Number of bytes to read.
     * @param buffer Location for storing the data.
     * @return was given amount of bytes read succesfully.
     */
    bool read(void* buffer, int size);

    /**
     * Attempt to read objects from the sockets. The call blocks until
     * there are minimum amount of expected bytes availabled in the socket.
     *
     * @param values Vector to which objects will be appended.
     * @tparam T type of expected object in the stream.
     * @return true if atleast one object was read.
     */
    template<typename T>
    bool read(QVector<T>& values);

    /**
     * Returns whether the socket is currently connected.
     *
     * @return is socket connected.
     */
    bool isConnected();

private:
    /**
     * Prefix text needed to be written to the sensor daemon socket connection
     * when establishing new session.
     */
    static const char* channelIDString;

    /**
     * Reads initial magic byte from the fresh connection.
     */
    bool readSocketTag();

    QLocalSocket* socket_; /**< socket data connection to sensord */
    bool tagRead_; /**< is initial magic byte read from the socket */
};

template<typename T>
bool SocketReader::read(QVector<T>& values)
{
    if (!socket_) {
        return false;
    }

    unsigned int count;
    if(!read((void*)&count, sizeof(unsigned int)))
    {
        socket_->readAll();
        return false;
    }
    if(count > 1000)
    {
        qWarning() << "Too many samples waiting in socket. Flushing it to empty";
        socket_->readAll();
        return false;
    }
    values.resize(values.size() + count);
    if(!read((void*)values.data(), sizeof(T) * count))
    {
        qWarning() << "Error occured while reading data from socket: " << socket_->errorString();
        socket_->readAll();
        return false;
    }
    return true;
}

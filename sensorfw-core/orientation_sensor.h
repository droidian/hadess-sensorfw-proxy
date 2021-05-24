/*
 * Copyright © 2016 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authored by: Alexandros Frantzis <alexandros.frantzis@canonical.com>
 */

#pragma once

#include "handler_registration.h"

#include <functional>

namespace repowerd
{

enum OrientationData
{
    Undefined = 0, /**< Orientation is unknown. */
    LeftUp,        /**< Device left side is up */
    RightUp,       /**< Device right side is up */
    BottomUp,      /**< Device bottom is up */
    BottomDown,    /**< Device bottom is down */
    FaceDown,      /**< Device face is down */
    FaceUp         /**< Device face is up */
};

using OrientationHandler = std::function<void(OrientationData)>;

class OrientationSensor
{
public:
    virtual ~OrientationSensor() = default;

    virtual HandlerRegistration register_orientation_handler(
        OrientationHandler const& handler) = 0;

    virtual void enable_orientation_events() = 0;
    virtual void disable_orientation_events() = 0;

protected:
    OrientationSensor() = default;
    OrientationSensor (OrientationSensor const&) = default;
    OrientationSensor& operator=(OrientationSensor const&) = default;
};

}

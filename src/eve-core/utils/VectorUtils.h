/*
    ------------------------------------------------------------------------------------
    LICENSE:
    ------------------------------------------------------------------------------------
    This file is part of EVEmu: EVE Online Server Emulator
    Copyright 2006 - 2021 The EVEmu Team
    ------------------------------------------------------------------------------------
    Author: celinor1982 / Adapted for Drone Spawning Utility
*/

#ifndef EVE_VECTOR_UTILS_H
#define EVE_VECTOR_UTILS_H

#include "math/GPoint.h"
#include "math/Vector3D.h"
#include "utils/misc.h"  // For MakeRandomFloat

/**
 * Generates a random GPoint in a spherical shell between innerRadius and outerRadius
 * centered around the given origin.
 *
 * @param origin        The center point to generate around.
 * @param innerRadius   The minimum distance from the origin.
 * @param outerRadius   The maximum distance from the origin.
 * @return              A randomized GPoint within the shell.
 */
inline GPoint MakeRandomPointNear(const GPoint& origin, double innerRadius, double outerRadius) {
    double theta = MakeRandomFloat(0.0, 2 * M_PI);
    double phi = MakeRandomFloat(0.0, 2 * M_PI);
    double radius = innerRadius + MakeRandomFloat(0.0, outerRadius - innerRadius);

    Vector3D offset;
    offset.x = radius * sin(theta) * cos(phi);
    offset.y = radius * sin(theta) * sin(phi);
    offset.z = radius * cos(theta);

    return origin + offset;
}

#endif // EVE_VECTOR_UTILS_H

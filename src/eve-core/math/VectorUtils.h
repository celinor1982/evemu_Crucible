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

#include "math/gpoint.h"
#include "utils/misc.h"  // For MakeRandomFloat

/**
 * Generates a random point within a spherical shell between innerRadius and outerRadius
 * around the given origin. Uses EVEmu's built-in GPoint mutation function.
 *
 * @param origin        The center point to spawn near.
 * @param innerRadius   Minimum distance from the origin.
 * @param outerRadius   Maximum distance from the origin.
 * @return              A new GPoint offset randomly in the defined shell volume.
 */
inline GPoint MakeRandomPointNear(const GPoint& origin, double innerRadius, double outerRadius) {
    // Copy the original position to avoid modifying the original reference
    GPoint pos = origin;

    // Modify the copied point by applying a random offset in the spherical shell
    pos.MakeRandomPointOnSphereLayer(innerRadius, outerRadius);

    // Return the randomized position
    return pos;
}

#endif // EVE_VECTOR_UTILS_H

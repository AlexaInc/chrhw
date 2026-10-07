#pragma once
// ===========================================================================
// FRONT SENSOR ARC - geometry and the decision table behind the avoidance.
//
// The rover carries three HC-SR04 on the printed brackets: one centre sensor
// plus the two side sensors, whose brackets splay them OUTWARDS. Together the
// three cones overlap into one fan, which is what removes the blind spot
// between the centre beam and each front corner:
//
//        left beam (45 deg)      centre beam      right beam (45 deg)
//             \                     |                    /
//              \_______   cone edge |  cone edge _______/
//              37 deg ...   8 deg   |   8 deg ... 37 deg
//
// A reading is meaningless on its own - it is taken ALONG that sensor's axis.
// Converting it with the mounting angle gives the two numbers that matter:
//
//     lateral room  = reading * sin(angle + half cone)
//     forward reach = reading * cos(angle - half cone)
//
// "lateral room" is how wide the gap on that side is (the OUTER cone edge is
// what covers the corner), "forward reach" is how soon something on that ray
// could be in the rover's way. A gap counts as passable only when it is wider
// than ROVER_HALF_WIDTH_CM + OBSTACLE_SIDE_MARGIN_CM.
//
// This header is deliberately free of Arduino/LEDC code so the decision table
// can be unit tested on a PC:  ./scripts/test-arc-math.sh
// ===========================================================================

#include <math.h>
#include "config.h"

static inline float arcDegToRad(float deg) { return deg * 0.01745329252f; }

static inline int arcClampAngleDeg(int deg) {
    if (deg < SENSOR_ANGLE_MIN_DEG) return SENSOR_ANGLE_MIN_DEG;
    if (deg > SENSOR_ANGLE_MAX_DEG) return SENSOR_ANGLE_MAX_DEG;
    return deg;
}

/** How much room to the SIDE a single reading proves, in cm. */
static inline float arcLateralRoomCm(long readingCm, int angleDeg) {
    const float angle = (float)arcClampAngleDeg(angleDeg) + (float)SENSOR_BEAM_HALF_DEG;
    return (float)readingCm * sinf(arcDegToRad(angle));
}

/** How close to the rover's front line something on that ray could be, in cm. */
static inline float arcForwardReachCm(long readingCm, int angleDeg) {
    float angle = (float)arcClampAngleDeg(angleDeg) - (float)SENSOR_BEAM_HALF_DEG;
    if (angle < 0.0f) angle = 0.0f;
    return (float)readingCm * cosf(arcDegToRad(angle));
}

struct ArcReading {
    long frontCm;
    long leftCm;
    long rightCm;
};

struct ArcDecision {
    /** Something inside the emergency ring: brake, no alternative. */
    bool emergency;
    /** The centre beam is inside the stop distance: steer around it. */
    bool aheadBlocked;
    /** That side is usable: no obstacle beside the rover and a wide-enough gap. */
    bool leftOpen;
    bool rightOpen;
    /** A plant inside the slow band that reaches into the swept path - shave
     *  off while still driving forward instead of stopping. */
    bool cornerIntrusion;
    /** Lateral room each side beam proves (cm). */
    float gapLeftCm;
    float gapRightCm;
    /** Gap width this rover needs to pass: half chassis + margin (cm). */
    float passableCm;
    /** -1 steer left, +1 steer right, 0 = neither side can be used. */
    int steerDir;
};

/** Which side to pass on: a blocked side is never chosen, and when both are
 *  usable the wider gap wins - that keeps the rover centred in the row instead
 *  of scraping along a plant. Returns -1 left, +1 right, 0 = neither side. */
static inline int arcChooseDir(bool leftOpen, bool rightOpen, float gapLeftCm, float gapRightCm) {
    if (leftOpen && rightOpen) return (gapRightCm >= gapLeftCm) ? 1 : -1;
    if (rightOpen) return 1;
    if (leftOpen) return -1;
    return 0;
}

/** Classify one scan of the arc. Pure function: same numbers in, same verdict
 *  out, which is why scripts/test-arc-math.cpp can pin the whole table. */
static inline ArcDecision arcEvaluate(const ArcReading &arc, int angleLeftDeg, int angleRightDeg) {
    ArcDecision d;
    d.gapLeftCm = arcLateralRoomCm(arc.leftCm, angleLeftDeg);
    d.gapRightCm = arcLateralRoomCm(arc.rightCm, angleRightDeg);
    const float reachLeft = arcForwardReachCm(arc.leftCm, angleLeftDeg);
    const float reachRight = arcForwardReachCm(arc.rightCm, angleRightDeg);
    d.passableCm = (float)ROVER_HALF_WIDTH_CM + (float)OBSTACLE_SIDE_MARGIN_CM;

    d.emergency = arc.frontCm <= OBSTACLE_EMERGENCY_CM;
    d.aheadBlocked = arc.frontCm <= OBSTACLE_STOP_CM;
    // A side is blocked when something is right beside the rover, or when that
    // ray sees an obstacle that is both close and too central to pass.
    const bool leftBlocked = (arc.leftCm <= OBSTACLE_SIDE_STOP_CM) ||
                             (reachLeft <= (float)OBSTACLE_STOP_CM && d.gapLeftCm < d.passableCm);
    const bool rightBlocked = (arc.rightCm <= OBSTACLE_SIDE_STOP_CM) ||
                              (reachRight <= (float)OBSTACLE_STOP_CM && d.gapRightCm < d.passableCm);
    d.leftOpen = !leftBlocked && d.gapLeftCm >= d.passableCm;
    d.rightOpen = !rightBlocked && d.gapRightCm >= d.passableCm;

    const bool cornerLeft = !d.aheadBlocked && !d.leftOpen && reachLeft <= (float)OBSTACLE_SLOW_CM;
    const bool cornerRight = !d.aheadBlocked && !d.rightOpen && reachRight <= (float)OBSTACLE_SLOW_CM;
    d.cornerIntrusion = cornerLeft || cornerRight;

    d.steerDir = arcChooseDir(d.leftOpen, d.rightOpen, d.gapLeftCm, d.gapRightCm);
    return d;
}

/** Which side is doing the blocking, for the operator-facing message. */
static inline const char *arcBlockedSideName(bool leftOpen, bool rightOpen) {
    if (!leftOpen && rightOpen) return "plant-left";
    if (!rightOpen && leftOpen) return "plant-right";
    return "plant-ahead";
}

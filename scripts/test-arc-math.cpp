// ===========================================================================
// Host-side unit test for the front-arc decision table (include/arc_math.h).
//
// The rover must drive AROUND a plant instead of stopping at it, and only stop
// when neither side gap is wide enough to pass. That whole policy is a pure
// function of three distances + the two bracket angles, so it is pinned here,
// on a PC, before anyone flashes a board:
//
//     ./scripts/test-arc-math.sh
//
// The numbers below are the real geometry of the machine: the side sensors sit
// on brackets splayed 45 deg outwards, an HC-SR04 cone is ~16 deg wide (8 deg
// half angle), the chassis is 26 cm across (13 cm half width) and a gap needs
// 4 cm of margin on top of that (17 cm) before it counts as passable.
// ===========================================================================
#include <cstdio>
#include <string>
#include <cmath>
#include "arc_math.h"

static int passed = 0;
static int failed = 0;

static void check(bool cond, const char *what) {
    if (cond) { passed++; printf("  ok    %s\n", what); }
    else { failed++; printf("  FAIL  %s\n", what); }
}

static bool near(float a, float b, float tol = 0.6f) { return fabsf(a - b) <= tol; }

// The machine geometry used by every case below (config.h defaults).
static const int ANGLE_L = SENSOR_ANGLE_LEFT_DEG;
static const int ANGLE_R = SENSOR_ANGLE_RIGHT_DEG;

static ArcDecision eval(long front, long left, long right) {
    return arcEvaluate({ front, left, right }, ANGLE_L, ANGLE_R);
}

int main() {
    printf("front-arc decision table (angles +/-%d deg, half cone %d deg, half width %d cm + %d cm margin)\n",
           ANGLE_L, SENSOR_BEAM_HALF_DEG, ROVER_HALF_WIDTH_CM, OBSTACLE_SIDE_MARGIN_CM);
    printf("passable gap = %.0f cm, stop band = %d cm, emergency = %d cm, slow band = %d cm\n\n",
           (float)ROVER_HALF_WIDTH_CM + (float)OBSTACLE_SIDE_MARGIN_CM,
           OBSTACLE_STOP_CM, OBSTACLE_EMERGENCY_CM, OBSTACLE_SLOW_CM);

    /* ---------------------------------------------------------- geometry -- */
    printf("geometry\n");
    // 100 cm along a 45 deg beam reaches 80 cm forward and 80 cm to the side
    // (45 + 8 = 53 deg edge -> sin 53 = 0.80).
    check(near(arcLateralRoomCm(100, 45), 79.9f), "100 cm on a 45 deg beam proves ~80 cm of lateral room");
    check(near(arcForwardReachCm(100, 45), 79.9f), "100 cm on a 45 deg beam is ~80 cm ahead (cos 37 deg)");
    check(near(arcLateralRoomCm(100, 0), 13.9f), "a straight-ahead beam can only claim room by its cone edge (sin 8 deg)");
    check(arcLateralRoomCm(100, 45) > arcLateralRoomCm(100, 0), "angling the beam outwards widens the proven gap");
    check(arcClampAngleDeg(120) == SENSOR_ANGLE_MAX_DEG, "an angle past 80 deg is clamped down to 80");
    check(arcClampAngleDeg(-5) == SENSOR_ANGLE_MIN_DEG, "a negative angle is clamped to 0");
    check(near(arcLateralRoomCm(22, 45), 17.6f) && near(arcLateralRoomCm(20, 45), 16.0f),
          "22 cm on the side beam is passable (17.6 cm), 20 cm is not (16.0 cm)");

    /* ------------------------------------------------------------- clear -- */
    printf("\nclear field\n");
    ArcDecision d = eval(300, 300, 300);
    check(!d.emergency && !d.aheadBlocked && d.leftOpen && d.rightOpen && !d.cornerIntrusion,
          "open field -> nothing blocked, both sides usable, no manoeuvre");

    // The Wokwi default scene (diagram.json: 49 / 197 / 400 cm) must NOT start
    // an avoidance - the centre is inside the slow band, so the rover only
    // slows down.
    d = eval(49, 197, 400);
    check(!d.emergency && !d.aheadBlocked && d.leftOpen && d.rightOpen,
          "Wokwi scene (49/197/400) -> no avoidance, just the slow band (rover drives on)");

    /* -------------------------------------------------- plant dead ahead -- */
    printf("\nplant straight ahead, both sides open\n");
    d = eval(24, 80, 150);
    check(d.aheadBlocked && !d.emergency, "centre at 24 cm -> path ahead blocked, not an emergency");
    check(d.leftOpen && d.rightOpen, "80 cm / 150 cm side beams -> both sides usable");
    check(d.steerDir == 1, "wider gap on the right (150 cm) -> steer RIGHT, not stop");
    check(std::string(arcBlockedSideName(d.leftOpen, d.rightOpen)) == "plant-ahead", "blocking side reported as plant-ahead");

    d = eval(24, 150, 80);
    check(d.steerDir == -1, "mirror case (150 left / 80 right) -> steer LEFT");

    /* ------------------------------------------------- obstacle on a side */
    printf("\nplant on one side only\n");
    d = eval(24, 18, 300);
    check(d.aheadBlocked && !d.leftOpen && d.rightOpen, "18 cm on the left at 45 deg -> left gap (14 cm) too narrow");
    check(d.steerDir == 1, "-> steer RIGHT, away from the plant");
    check(std::string(arcBlockedSideName(d.leftOpen, d.rightOpen)) == "plant-left", "blocking side reported as plant-left");

    d = eval(24, 300, 18);
    check(d.leftOpen && !d.rightOpen && d.steerDir == -1, "mirror case -> steer LEFT");

    /* ------------------------------------------------------- no way past -- */
    printf("\nno gap wide enough (the ONLY planned stop)\n");
    d = eval(24, 20, 20);
    check(!d.leftOpen && !d.rightOpen && d.steerDir == 0,
          "plants 20 cm on both beams (16 cm gaps) -> no passable side: stop and report no-path");
    d = eval(24, 10, 300);
    check(!d.leftOpen && d.rightOpen && d.steerDir == 1,
          "something 10 cm from the left ray -> never steer into it, use the right side");

    /* ---------------------------------------------------------- emergency -- */
    printf("\nemergency ring\n");
    d = eval(OBSTACLE_EMERGENCY_CM - 1, 300, 300);
    check(d.emergency, "inside the emergency ring -> brake, even with both sides open");
    d = eval(OBSTACLE_EMERGENCY_CM + 1, 300, 300);
    check(!d.emergency, "just outside the emergency ring -> no brake");

    /* ------------------------------------------------------- corner nudge -- */
    printf("\ncorner intrusion (driving on, just shaved off)\n");
    d = eval(60, 20, 300);
    check(!d.aheadBlocked && d.cornerIntrusion && !d.leftOpen && d.rightOpen,
          "plant at the front-left corner with the centre clear -> shave off to the right, still moving");
    d = eval(60, 25, 300);
    check(!d.cornerIntrusion, "25 cm on the left beam (20 cm gap) is no longer an intrusion");

    /* ------------------------------------------------------ angle changes -- */
    printf("\nre-bolted brackets (panel angles)\n");
    // A bracket bent to 25 deg barely proves any lateral room, so the same
    // reading stops counting as a gap - the operator sets the real angle.
    ArcDecision narrow = arcEvaluate({ 24, 22, 300 }, 25, 25);
    check(!narrow.leftOpen, "22 cm on a 25 deg bracket (15 cm gap) is not passable");
    ArcDecision wide = arcEvaluate({ 24, 22, 300 }, 60, 60);
    check(wide.leftOpen, "the same 22 cm on a 60 deg bracket (20 cm gap) IS passable");

    printf("\n%d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}

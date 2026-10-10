#include "wokwi-api.h"

/* Visual-only DC motor placeholder for the rover's L298N outputs.
   Both pins are passive inputs: this chip does not model rotation, current,
   stall, back-EMF, noise, or electrical/mechanical safety behavior. */
typedef struct {
  pin_t motor_plus;
  pin_t motor_minus;
} dc_motor_t;

static dc_motor_t motor;

void chip_init(void) {
  motor.motor_plus = pin_init("M+", INPUT);
  motor.motor_minus = pin_init("M-", INPUT);
}

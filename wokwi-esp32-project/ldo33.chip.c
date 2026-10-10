#include "wokwi-api.h"
#include <stdlib.h>

/* AMS1117-style regulator placeholder.
   Wokwi's digital chip model marks the output as HIGH for wiring visibility;
   it does NOT model output voltage, current limit, dropout, thermal behavior,
   or reverse current. Verify a real regulator and its datasheet before use. */
typedef struct { pin_t p1; pin_t p2; pin_t p3; pin_t p4; } chip_t;

void chip_init(void) {
  chip_t *c = malloc(sizeof(chip_t));
  c->p1 = pin_init("VIN+", INPUT);
  c->p2 = pin_init("VIN-", INPUT);
  c->p3 = pin_init("3V3", OUTPUT_HIGH);
  c->p4 = pin_init("GND", OUTPUT_LOW);
}

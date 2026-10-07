#include "wokwi-api.h"
#include <stdlib.h>

/* 3.3V regulator AMS1117 - protection/passive part, added for the protection circuit.
   Separate 3.3V supply for the ESP32-CAM.
   Only the pins are declared so the diagram can show and wire the part;
   it does not change signal behaviour (passive component). */
typedef struct { pin_t p1; pin_t p2; pin_t p3; pin_t p4; } chip_t;

void chip_init(void) {
  chip_t *c = malloc(sizeof(chip_t));
  c->p1 = pin_init("VIN+", INPUT);
  c->p2 = pin_init("VIN-", INPUT);
  c->p3 = pin_init("3V3", INPUT);
  c->p4 = pin_init("GND", INPUT);
}

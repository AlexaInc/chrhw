#include "wokwi-api.h"
#include <stdlib.h>

typedef struct { pin_t vinp,vinn,vout,gnd; uint32_t voltage; } chip_t;
void chip_init(void) {
  chip_t *c = malloc(sizeof(chip_t));
  c->vinp=pin_init("VIN+",INPUT); c->vinn=pin_init("VIN-",INPUT);
  c->vout=pin_init("5V",OUTPUT_HIGH); c->gnd=pin_init("GND",OUTPUT_LOW);
  c->voltage=attr_init_float("outputVoltage",5.0);
}

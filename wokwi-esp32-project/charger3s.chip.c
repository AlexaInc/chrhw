#include "wokwi-api.h"
#include <stdlib.h>

typedef struct { pin_t inp, inn, batp, batn; uint32_t voltage; } chip_t;

void chip_init(void) {
  chip_t *chip = malloc(sizeof(chip_t));
  // Spaces preserve compatibility with the supplied precompiled Wokwi chip.
  chip->inp = pin_init("IN+ ", INPUT);
  chip->inn = pin_init("IN- ", INPUT);
  chip->batp = pin_init("B+", OUTPUT_HIGH);
  chip->batn = pin_init("B- ", OUTPUT_LOW);
  chip->voltage = attr_init_float("outputVoltage", 12.6);
}

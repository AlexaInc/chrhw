#include "wokwi-api.h"
#include <stdlib.h>

typedef struct { pin_t positive, negative; uint32_t voltage; } chip_t;

void chip_init(void) {
  chip_t *chip = malloc(sizeof(chip_t));
  chip->positive = pin_init("POS", OUTPUT_HIGH);
  chip->negative = pin_init("NEG", OUTPUT_LOW);
  chip->voltage = attr_init_float("voltage", 12.0);
}

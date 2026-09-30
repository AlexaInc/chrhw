#include "wokwi-api.h"
#include <stdlib.h>

typedef struct { pin_t pos; pin_t neg; uint32_t voltage; } chip_t;
void chip_init(void) {
  chip_t *c = malloc(sizeof(chip_t));
  c->pos = pin_init("POS", OUTPUT_HIGH);
  c->neg = pin_init("NEG", OUTPUT_LOW);
  c->voltage = attr_init_float("voltage", 3.7);
}

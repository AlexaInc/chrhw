#include "wokwi-api.h"
#include <stdlib.h>

/* Fuse 5A (blade) - protection/passive part, added for the protection circuit.
   Series fuse right after BMS P+ / in the 12V feed.
   Only the pins are declared so the diagram can show and wire the part;
   it does not change signal behaviour (passive component). */
typedef struct { pin_t p1; pin_t p2; } chip_t;

void chip_init(void) {
  chip_t *c = malloc(sizeof(chip_t));
  c->p1 = pin_init("1", INPUT);
  c->p2 = pin_init("2", INPUT);
}

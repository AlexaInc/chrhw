#include "wokwi-api.h"
#include <stdlib.h>

typedef struct { pin_t bneg,b1,b2,bpos,pneg,ppos; } chip_t;
void chip_init(void) {
  chip_t *c = malloc(sizeof(chip_t));
  c->bneg=pin_init("B-",INPUT); c->b1=pin_init("B1",INPUT);
  c->b2=pin_init("B2",INPUT); c->bpos=pin_init("B+",INPUT);
  c->pneg=pin_init("P-",OUTPUT_LOW); c->ppos=pin_init("P+",OUTPUT_HIGH);
}

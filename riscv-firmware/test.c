#include <stdint.h>
volatile uint32_t counter = 0;
void increment() {
    __sync_synchronize();
    counter++;
}

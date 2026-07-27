#include "bss_only.h"

unsigned int luna_test_bss_only_value;

unsigned int c_bss_only_round_trip(void) {
    luna_test_bss_only_value = 42U;
    return luna_test_bss_only_value;
}

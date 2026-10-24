#pragma once

#include <stdint.h>
#include "btstack_run_loop.h"

const btstack_run_loop_t *btstack_run_loop_a4f_get_instance(void);
uint32_t btstack_run_loop_a4f_time_ms(void);

#ifndef STABILIZATION_TASK_H_INCLUDED
#define STABILIZATION_TASK_H_INCLUDED

#include "esp_err.h"
#include "motor_control.h"
#include "PID_stabilization.h"

esp_err_t stabilization_init(PID_controller *roll, PID_controller *pitch);
void stabilization_reset(PID_controller *roll, PID_controller *pitch);
esp_err_t stabilization_update(PID_controller *roll, PID_controller *pitch);
#endif
#ifndef PID_STABILIZATION_H_INCLUDED
#define PID_STABILIZATION_H_INCLUDED

typedef struct {
    // Basic PID
    float Kp;                   // proportional gain constant
    float Ki;                   // integral gain constant
    float Kd;                   // derivative gain constant
    float prev_err;             // Previous Error
    float integral_accum_err;   // Accumulated Integral Error
    float max;                  // Maximum possible PID Command
    float min;                  // Minimum possible PID Command

    // Derivative Noise Filtering
    float T_C;                  // Derivative Filter Time Constant
    float prev_deriv;           // Previous derivative value

} PID_controller;

void pid_init (PID_controller *pid);
void pid_tune (PID_controller *pid, float Kp, float Ki, float Kd, float T_C);
float pid_calculate (PID_controller *pid, float measured_angle, float target_angle);
void pid_reset (PID_controller *pid);

#endif
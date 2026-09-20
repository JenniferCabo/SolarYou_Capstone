#include <stdio.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "motor_control.h"
#include "PID_stabilization.h"

#define PI     3.14159265f 

//basic testing values, tuned params
float Kp = 0.8f;
float Ki = 0.05f;
float Kd = 0.10f;
float T_C = 0.050f;

//simulated real world values
float target_angle = 0;

void app_main(void)
{
    PID_controller roll;
    PID_controller pitch;

    esp_err_t try_motor_init = motor_init();
    if(try_motor_init != ESP_OK){
        printf("Motor command failed: %s\n", esp_err_to_name(try_motor_init));
        return;
    }

    pid_init(&roll);
    pid_init(&pitch);


    pid_tune(&roll, Kp, Ki, Kd, T_C);
    pid_tune(&pitch, Kp, Ki, Kd, T_C);

    float t = 0.0f;

    while (t < 100.0f){
        float roll_imu = 8.0f * sinf(2.0f * PI * t / 10.0f);

        float pitch_imu = 5.0f * sinf((2.0f * PI * t / 14.0f) + PI / 3.0f);

         // fetch measured(IMU) and target(light) angle

        // determine PID command
        float roll_command = pid_calculate(&roll, roll_imu, target_angle);
        float pitch_command = pid_calculate(&pitch, pitch_imu, target_angle);

        // map PID command angles to servo angles
        float servo_roll_angle = roll_command + 90;
        float servo_pitch_angle = pitch_command + 90;

        esp_err_t try_command_motor = command_motor_angle(servo_roll_angle, servo_pitch_angle);
        if (try_command_motor != ESP_OK){
            printf("Motor command failed: %s\n", esp_err_to_name(try_command_motor));
            return;
        }
 
        t += 0.010f;
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    esp_err_t try_motor_del = motor_del();
    if(try_motor_del != ESP_OK){
        printf("Motor command failed: %s\n", esp_err_to_name(try_motor_del));
        return;
    }

}

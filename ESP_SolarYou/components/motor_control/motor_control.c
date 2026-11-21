#include "iot_servo.h"
#include "motor_control.h"
#include <math.h>
#include "board_config.h"
#include "esp_log.h"

static const char *TAG = "Motor Cntrl";

// define servos
static servo_handle_t roll_servo = NULL;
static servo_handle_t pitch_servo = NULL;

esp_err_t motor_init(float roll_center, float pitch_center)
{
    if (!isfinite(roll_center) ||
    !isfinite(pitch_center) ||
    roll_center < MIN_SERVO_ANGLE_ROLL ||
    roll_center > MAX_SERVO_ANGLE_ROLL ||
    pitch_center < MIN_SERVO_ANGLE_PITCH ||
    pitch_center > MAX_SERVO_ANGLE_PITCH ||
    roll_center > 180.0f ||
    pitch_center > 180.0f){

        ESP_LOGE(TAG, "Motor initialization failed: Invalid servo center");
        return ESP_ERR_INVALID_ARG;
    }

    if (roll_servo || pitch_servo){
        ESP_LOGE(TAG, "Motor initialization failed: pre-existing motor");
        return ESP_ERR_INVALID_STATE;
    }

    //configure roll servo to LEDC Low, provision LEDC channel and GPIO 
    servo_config_t servo_cfg = SERVO_CONFIG_DEFAULT(LEDC_LOW_SPEED_MODE, LEDC_TIMER_0, LEDC_CHANNEL_0, SERVO_CH0_PIN);
    
    servo_cfg.initial_angle = roll_center;
    esp_err_t err = iot_servo_new(&servo_cfg, &roll_servo);
    if (err != ESP_OK){
        ESP_LOGE(TAG, "Motor initialization failed: roll motor provisioning failed %s", esp_err_to_name(err));        
        return err;
    }

    //configure pitch servo to LEDC Low, provision LEDC channel and GPIO
    servo_cfg.channel = LEDC_CHANNEL_1;
    servo_cfg.gpio_num = SERVO_CH1_PIN;

    servo_cfg.initial_angle = pitch_center;
    err = iot_servo_new(&servo_cfg, &pitch_servo);
    if (err != ESP_OK){
        ESP_LOGE(TAG, "Motor initialization failed: pitch motor provisioning failed %s", esp_err_to_name(err));
        
         esp_err_t cleanup_err = motor_del();

        if (cleanup_err != ESP_OK) {
            ESP_LOGE(TAG, "Motor initialization cleanup failed: %s",
                    esp_err_to_name(cleanup_err));
        }

        return err;
    }

    return ESP_OK;
}


// abstracted motor movement function
esp_err_t command_motor_angle(float roll_angle, float pitch_angle)
{
    if (roll_servo == NULL || pitch_servo == NULL){
        ESP_LOGE(TAG, "Angle commanding failed: motor not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (!isfinite(roll_angle) || !isfinite(pitch_angle)){
        ESP_LOGE(TAG, "Angle commanding failed: invalid angle");        
        return ESP_ERR_INVALID_ARG;
    }
    
    if (roll_angle < MIN_SERVO_ANGLE_ROLL){
        roll_angle = MIN_SERVO_ANGLE_ROLL;
    }else if (roll_angle > MAX_SERVO_ANGLE_ROLL){
        roll_angle = MAX_SERVO_ANGLE_ROLL;
    }

    if (pitch_angle < MIN_SERVO_ANGLE_PITCH){
        pitch_angle = MIN_SERVO_ANGLE_PITCH;
    }else if (pitch_angle > MAX_SERVO_ANGLE_PITCH){
        pitch_angle = MAX_SERVO_ANGLE_PITCH;
    }

// might need review since it bails on roll fail (is that bad given next iter?)
    esp_err_t err = iot_servo_write_angle(roll_servo, roll_angle);
    if (err != ESP_OK){
        ESP_LOGE(TAG, "Angle commanding failed: write to roll servo failed: %s", esp_err_to_name(err));        
        return err;
    }

    err = iot_servo_write_angle(pitch_servo, pitch_angle);
    if (err != ESP_OK){
        ESP_LOGE(TAG, "Angle commanding failed: write to pitch servo failed: %s", esp_err_to_name(err));        
        return err;
    }

    return ESP_OK;
}

esp_err_t motor_del(void)
{
    esp_err_t err = ESP_OK;

    if (roll_servo) {
        esp_err_t r = iot_servo_del(roll_servo);
        if (r != ESP_OK){
            err = r;
            ESP_LOGE(TAG, "Motor deletion failed: roll motor: %s", esp_err_to_name(err));
        }else{
            roll_servo = NULL;
        }
    }

    if (pitch_servo) {
        esp_err_t r = iot_servo_del(pitch_servo);
        if (r != ESP_OK){
            err = r;
            ESP_LOGE(TAG, "Motor deletion failed: pitch motor: %s", esp_err_to_name(err));
        }else{
            pitch_servo = NULL;
        }
    }

    return err;
}
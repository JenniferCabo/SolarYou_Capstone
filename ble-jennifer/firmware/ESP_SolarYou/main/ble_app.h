#ifndef SOLARYOU_BLE_APP_H
#define SOLARYOU_BLE_APP_H
#include <stdbool.h>

/* Hooks for the existing single sensor-owner loop. No motor/USB changes. */
void ble_app_start(void);
void ble_app_update_imu(float pitch, float roll, bool valid);
void ble_app_update_targets(float pitch, float roll, bool valid);
void ble_app_poll(void);
#endif

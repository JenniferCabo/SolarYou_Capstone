#ifndef SOLARYOU_STARTUP_STUBS_H
#define SOLARYOU_STARTUP_STUBS_H
#include <stdint.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
const char *esp_err_to_name(esp_err_t error);
int64_t esp_timer_get_time(void);
void startup_log(const char *tag, const char *format, ...);
void startup_delay(unsigned ticks);
#define ESP_LOGE(...) startup_log(__VA_ARGS__)
#define ESP_LOGW(...) startup_log(__VA_ARGS__)
#define ESP_LOGI(...) startup_log(__VA_ARGS__)
#define vTaskDelay(ticks) startup_delay(ticks)
#endif

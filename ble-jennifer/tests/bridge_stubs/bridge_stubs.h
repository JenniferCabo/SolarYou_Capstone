#ifndef SOLARYOU_BRIDGE_STUBS_H
#define SOLARYOU_BRIDGE_STUBS_H
#include <stdint.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
const char *esp_err_to_name(esp_err_t result);
int64_t esp_timer_get_time(void);
void bridge_log(const char *tag, const char *format, ...);
#define ESP_LOGE(...) bridge_log(__VA_ARGS__)
#endif

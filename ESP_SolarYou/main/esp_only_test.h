#ifndef SOLARYOU_ESP_ONLY_TEST_H
#define SOLARYOU_ESP_ONLY_TEST_H

/* Separate runtime mode. Never calls sensor, calibration or motor APIs. */
void esp_only_test_run(void);

#endif

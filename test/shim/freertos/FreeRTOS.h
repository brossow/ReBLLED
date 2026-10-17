#pragma once
#define portMAX_DELAY 0xFFFFFFFFu
#define pdMS_TO_TICKS(x) (x)
typedef void *TaskHandle_t;
typedef int BaseType_t;
#define pdPASS 1
#define CONFIG_FREERTOS_UNICORE 1
inline BaseType_t xTaskCreate(void (*)(void *), const char *, unsigned, void *, unsigned, TaskHandle_t *h) { if (h) *h = (TaskHandle_t)1; return pdPASS; }
inline void vTaskDelay(unsigned) {}
inline void vTaskDelete(void *) {}
inline unsigned uxTaskGetStackHighWaterMark(void *) { return 0; }

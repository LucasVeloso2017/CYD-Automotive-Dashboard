#pragma once
#include <cstdint>
void vTaskDelay(uint32_t ticks);
int xTaskCreate(void (*fn)(void *), const char *name, uint32_t stack, void *arg, int prio, TaskHandle_t *out);
/* No PC nao ha cores: vira o xTaskCreate do teste. */
static inline int xTaskCreatePinnedToCore(void (*fn)(void *), const char *name, uint32_t stack, void *arg,
                                          int prio, TaskHandle_t *out, int)
{
    return xTaskCreate(fn, name, stack, arg, prio, out);
}
void vTaskDelete(TaskHandle_t t);
/* Sobra minima de pilha: no PC nao ha pilha de task para medir. */
static inline uint32_t uxTaskGetStackHighWaterMark(TaskHandle_t) { return 0; }

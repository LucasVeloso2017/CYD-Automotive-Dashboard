#pragma once
typedef void *TaskHandle_t;
#define pdMS_TO_TICKS(x) (x)
#define pdPASS 1
/* secao critica: no PC o teste e single-thread */
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(m) ((void)(m))
#define portEXIT_CRITICAL(m)  ((void)(m))

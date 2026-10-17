#pragma once
// Single-threaded tests: the mutex only has to count, and must be recursive.
typedef int *SemaphoreHandle_t;
inline int fake_mutex_depth = 0;
inline SemaphoreHandle_t xSemaphoreCreateRecursiveMutex() { return &fake_mutex_depth; }
inline int xSemaphoreTakeRecursive(SemaphoreHandle_t m, unsigned) { (*m)++; return 1; }
inline int xSemaphoreGiveRecursive(SemaphoreHandle_t m) { (*m)--; return 1; }

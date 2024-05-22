#pragma once
#include "typedef.h"
#include <stdlib.h>
#include <android/log.h>
#include <android/asset_manager.h>

#undef DEBUG_PRINT
#ifdef NEKRO_DEBUG
#define DEBUG_PRINT(prio, fmt, ...) Print(prio, "%s: " fmt " (%s, line %d)\n", \
	__func__, ##__VA_ARGS__, __FILE__, __LINE__)
#else
#define DEBUG_PRINT(prio, fmt, ...)
#endif

#undef DEBUG_LOG
#define DEBUG_LOG(fmt, ...) DEBUG_PRINT(ANDROID_LOG_DEBUG, fmt, ##__VA_ARGS__)

#undef DEBUG_ERROR
#define DEBUG_ERROR(fmt, ...) DEBUG_PRINT(ANDROID_LOG_FATAL, fmt, ##__VA_ARGS__); abort()

void Print(int prio, const char* fmt, ...);
char* AllocFileBytes(const char* fname, u32& outLength, AAssetManager *assetManager);
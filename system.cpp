#include "system.h"
#include <cstdio>
#include <stdarg.h>
#include <iostream>
#include <fstream>
#include <android/log.h>
#include <android/asset_manager.h>

void Print(const char* fmt, ...) {
    char s[1025];
    va_list args;
    va_start(args, fmt);
    vsprintf(s, fmt, args);
    va_end(args);
    __android_log_write(ANDROID_LOG_DEBUG, "vrengine", s);
}

// Memory is owned by caller
char* AllocFileBytes(const char* fname, u32& outLength, AAssetManager *assetManager) {
    AAsset *file = AAssetManager_open(assetManager, fname, AASSET_MODE_BUFFER);
	auto fileSize = AAsset_getLength(file);
	char* buffer = (char*)calloc(1, fileSize);
    AAsset_read(file, (void*)buffer, fileSize);
    AAsset_close(file);

	outLength = (u32)fileSize;
	return buffer;
}
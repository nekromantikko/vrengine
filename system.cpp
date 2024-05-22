#include "system.h"
#include <cstdio>
#include <stdarg.h>
#include <iostream>
#include <fstream>

void Print(int prio, const char* fmt, ...) {
	char s[1025];
	va_list args;
	va_start(args, fmt);
	vsprintf(s, fmt, args);
	va_end(args);
	__android_log_write(prio, "vrengine", s);
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
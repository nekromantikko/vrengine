#include <vulkan/vulkan.h>
#define XR_USE_GRAPHICS_API_VULKAN
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include "system.h"

extern "C" void android_main(struct android_app *app) {
    DEBUG_LOG("Hello world!\n");
}
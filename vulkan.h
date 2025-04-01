#ifdef __APPLE__
#define VK_USE_PLATFORM_MACOS_MVK 1
#define VK_USE_PLATFORM_METAL_EXT 1
#endif
#ifdef _WIN32
#define VK_USE_PLATFORM_WIN32_KHR 1
#endif
#define VK_ENABLE_BETA_EXTENSIONS 1
#ifdef STATIC_MVK
#include <vulkan/vulkan.h>
#else
#include "volk.h"
#endif
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 0

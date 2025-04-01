#include "ui_renderer_vulkan.h"
#include "ui_common_sdl.h"
#include "main.h"
#include "ui_main_nk.h"

/* nuklear - 1.32.0 - public domain */
#include "nuklear_sdl_vulkan.h"
#include "vk_mem_alloc.h"

#include "placebo.h"
#include "rashader.h"
#include <libplacebo/vulkan.h>

#include "ui_renderer_metal.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>

#define VK_VERSION VK_API_VERSION_1_3
#define VK_MIN_VERSION VK_API_VERSION_1_1
#define MAX_VERTEX_BUFFER 512 * 1024
#define MAX_ELEMENT_BUFFER 128 * 1024
// two screens for top ctx and one for bottom, times two for upscaled, plus cursor
// top and bottom ctxs have separate pools
#define VK_VIEW_DESC_COUNT_MAX (SCREEN_COUNT * 2 + 1)

/* ===============================================================
 *
 *                          DEMO
 *
 * ===============================================================*/

static const char *validation_layer_name = "VK_LAYER_KHRONOS_validation";

struct queue_family_indices {
    int graphics;
    int present;
};

struct swap_chain_support_details {
    VkSurfaceCapabilitiesKHR capabilities;
    VkSurfaceFormatKHR *formats;
    uint32_t formats_len;
    VkPresentModeKHR *present_modes;
    uint32_t present_modes_len;
};

void swap_chain_support_details_free(
    struct swap_chain_support_details *swap_chain_support) {
    if (swap_chain_support->formats_len > 0) {
        free(swap_chain_support->formats);
        swap_chain_support->formats = NULL;
    }
    if (swap_chain_support->present_modes_len > 0) {
        free(swap_chain_support->present_modes);
        swap_chain_support->present_modes = NULL;
    }
}

struct vulkan_demo {
    SDL_Window *win;
    uint32_t win_width_pixel, win_height_pixel;
    float win_scale;
    bool resizing;
    bool dynamic_rendering;
    bool metal_objects;
    VkPhysicalDeviceFeatures2 physical_features2;
    VkPhysicalDeviceVulkan11Features physical_features11;
    VkPhysicalDeviceVulkan12Features physical_features12;
    VkPhysicalDeviceVulkan13Features physical_features13;
    VkPhysicalDeviceDynamicRenderingFeatures dyn_ren_features;
    VkPhysicalDeviceSynchronization2Features syn2_features;
    VkPhysicalDevicePortabilitySubsetFeaturesKHR port_sub_features;
    const char **extensions;
    uint32_t num_extensions;
    VkInstance instance;
    VkDebugUtilsMessengerEXT debug_messenger;
    uint32_t image_index;
    VkClearValue clear_color;
    VkSurfaceKHR surface;
    VkPhysicalDevice physical_device;
    struct queue_family_indices indices;
    VkDevice device;
    VkQueue graphics_queue;
    VkQueue present_queue;
    VkSampler sampler;

    VkSwapchainKHR swap_chain;
    VkImage *swap_chain_images;
    uint32_t swap_chain_images_len;
    VkImageView *swap_chain_image_views;
    VkFormat swap_chain_image_format;
    VkExtent2D swap_chain_image_extent;

    VkImage *overlay_images;
    VkImageView *overlay_image_views;
    VkDeviceMemory *overlay_image_memories;

    VkRenderPass render_pass;
    VkRenderPass cursor_render_pass;
    VkFramebuffer *framebuffers;
    VkDescriptorSetLayout descriptor_set_layout;
    VkDescriptorPool descriptor_pool;
    VkDescriptorSet *descriptor_sets;
    VkPipelineLayout pipeline_layout;
    VkPipeline pipeline;
    VkPipeline data_pipeline;
    VkPipeline cursor_pipeline;
    VkCommandPool command_pool;
    VkCommandBuffer *command_buffers;
    VkCommandBuffer *upload_command_buffers[SCREEN_COUNT];
    VkCommandBuffer *libra_command_buffers[SCREEN_COUNT];
    VkSemaphore upload_sem[SCREEN_COUNT];
    VkSemaphore libra_sem[SCREEN_COUNT];
    VkSemaphore image_available;
    VkSemaphore render_finished;

    VkFence render_fence;

#ifdef __APPLE__
    MTLCommandQueue_id mtl_queue;
    MTLSharedEvent_id upload_evt[SCREEN_COUNT];
    MTLSharedEvent_id libra_evt[SCREEN_COUNT];
    uint64_t upload_val[SCREEN_COUNT];
    uint64_t libra_val[SCREEN_COUNT];
#endif
};

static bool use_placebo;
static bool use_rashader;

static VKAPI_ATTR VkBool32 VKAPI_CALL vulkan_debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
    VkDebugUtilsMessageTypeFlagsEXT message_type,
    const VkDebugUtilsMessengerCallbackDataEXT *callback_data,
    void *user_data
) {
    (void)message_severity;
    (void)message_type;
    (void)user_data;
    err_log("validation layer: %s\n", callback_data->pMessage);

    return VK_FALSE;
}

static bool check_validation_layer_support() {
    uint32_t layer_count;
    bool ret = false;
    VkResult result;
    uint32_t i;
    VkLayerProperties *available_layers = NULL;

    result = vkEnumerateInstanceLayerProperties(&layer_count, NULL);
    if (result != VK_SUCCESS) {
        err_log("vkEnumerateInstanceLayerProperties failed: %d\n",
                result);
        return ret;
    }

    available_layers = malloc(layer_count * sizeof(VkLayerProperties));
    result = vkEnumerateInstanceLayerProperties(&layer_count, available_layers);
    if (result != VK_SUCCESS) {
        err_log("vkEnumerateInstanceLayerProperties failed: %d\n",
                result);
        goto cleanup;
    }

#ifndef NDEBUG
    err_log("Available vulkan layers:\n");
#endif
    for (i = 0; i < layer_count; i++) {
        #ifndef NDEBUG
        err_log("  %s\n", available_layers[i].layerName);
#endif
        if (strcmp(validation_layer_name, available_layers[i].layerName) == 0) {
            ret = true;
            break;
        }
    }
cleanup:
    free(available_layers);
    return ret;
}

static VkResult create_debug_utils_messenger_ext(
    VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT *pCreateInfo,
    const VkAllocationCallbacks *pAllocator,
    VkDebugUtilsMessengerEXT *pDebugMessenger) {
    PFN_vkCreateDebugUtilsMessengerEXT func =
        (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
            instance, "vkCreateDebugUtilsMessengerEXT");
    if (func != NULL) {
        return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
    } else {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
}

static bool create_debug_callback(struct vulkan_demo *demo) {
    VkResult result;

    VkDebugUtilsMessengerCreateInfoEXT create_info;
    memset(&create_info, 0, sizeof(VkDebugUtilsMessengerCreateInfoEXT));
    create_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    create_info.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    create_info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                              VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                              VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    create_info.pfnUserCallback = vulkan_debug_callback;

    result = create_debug_utils_messenger_ext(demo->instance, &create_info,
                                              NULL, &demo->debug_messenger);
    if (result != VK_SUCCESS) {
        err_log("create_debug_utils_messenger_ext failed %d\n", result);
        return false;
    }
    return true;
}

static bool create_instance(struct vulkan_demo *demo) {
    uint32_t i;
    uint32_t available_instance_extension_count;
    VkResult result;
    VkExtensionProperties *available_instance_extensions = NULL;
    bool ret = false;
    VkApplicationInfo app_info;
    VkInstanceCreateInfo create_info;
    uint32_t sdl_extension_count;
    uint32_t enabled_extension_count;
    const char **enabled_extensions = NULL;
    const char *const *sdl_enabled_extensions;
    bool validation_layers_installed;

    validation_layers_installed = check_validation_layer_support();

#ifndef NDEBUG
    if (!validation_layers_installed) {
        err_log(
                "Couldn't find validation layer %s. Continuing without "
                "validation layers.\n",
                validation_layer_name);
    }
#endif
    result = vkEnumerateInstanceExtensionProperties(
        NULL, &available_instance_extension_count, NULL);
    if (result != VK_SUCCESS) {
        err_log("vkEnumerateInstanceExtensionProperties failed %d\n",
                result);
        return ret;
    }

    available_instance_extensions = malloc(available_instance_extension_count *
                                           sizeof(VkExtensionProperties));

    result = vkEnumerateInstanceExtensionProperties(
        NULL, &available_instance_extension_count,
        available_instance_extensions);

    if (result != VK_SUCCESS) {
        err_log("vkEnumerateInstanceExtensionProperties failed %d\n",
                result);
        goto cleanup;
    }
#ifndef NDEBUG
    err_log("available instance extensions:\n");
    for (i = 0; i < available_instance_extension_count; i++) {
        err_log("  %s\n", available_instance_extensions[i].extensionName);
    }
#endif

    sdl_enabled_extensions = SDL_Vulkan_GetInstanceExtensions(&sdl_extension_count);
    if (!sdl_enabled_extensions) {
        err_log("SDL_Vulkan_GetInstanceExtensions failed: %s\n",
                SDL_GetError());
        goto cleanup;
    }

#ifdef NDEBUG
    validation_layers_installed = 0;
#endif

#if 1
    enabled_extension_count =
        sdl_extension_count + (validation_layers_installed ? 1 : 0);

    enabled_extensions = malloc(enabled_extension_count * sizeof(char *));
    memcpy(enabled_extensions, sdl_enabled_extensions, sdl_extension_count * sizeof(char *));

    if (validation_layers_installed) {
        enabled_extensions[sdl_extension_count] =
            VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
    }

#ifndef NDEBUG
    err_log("Trying to enable the following instance extensions:\n");
    for (i = 0; i < enabled_extension_count; i++) {
        err_log("%s\n", enabled_extensions[i]);
    }
#endif
    for (i = 0; i < enabled_extension_count; i++) {
        int extension_missing = 1;
        uint32_t j;
        for (j = 0; j < available_instance_extension_count; j++) {
            if (strcmp(enabled_extensions[i],
                       available_instance_extensions[j].extensionName) == 0) {
                extension_missing = 0;
                break;
            }
        }
        if (extension_missing) {
            err_log("Extension %s is missing\n", enabled_extensions[i]);
            return ret;
        }
    }
#else
    enabled_extension_count = available_instance_extension_count;
    enabled_extensions = malloc(available_instance_extension_count * sizeof(char *));
    for (i = 0; i < available_instance_extension_count; ++i) {
        enabled_extensions[i] = available_instance_extensions[i].extensionName;
    }
#endif

    memset(&app_info, 0, sizeof(VkApplicationInfo));
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "Demo";
    app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.pEngineName = "No Engine";
    app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.apiVersion = VK_VERSION;

    memset(&create_info, 0, sizeof(VkInstanceCreateInfo));
    create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create_info.pApplicationInfo = &app_info;
    create_info.enabledExtensionCount = enabled_extension_count;
    create_info.ppEnabledExtensionNames = enabled_extensions;
#ifdef __APPLE__
    create_info.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    VkExportMetalObjectCreateInfoEXT metal_info_queue = { VK_STRUCTURE_TYPE_EXPORT_METAL_OBJECT_CREATE_INFO_EXT };
    metal_info_queue.exportObjectType = VK_EXPORT_METAL_OBJECT_TYPE_METAL_COMMAND_QUEUE_BIT_EXT;;
    create_info.pNext = &metal_info_queue;
#endif
    if (validation_layers_installed) {
        create_info.enabledLayerCount = 1;
        create_info.ppEnabledLayerNames = &validation_layer_name;
    }
    result = vkCreateInstance(&create_info, NULL, &demo->instance);
    if (result != VK_SUCCESS) {
        err_log("vkCreateInstance result %d\n", result);
        return ret;
    }
    if (validation_layers_installed) {
        ret = create_debug_callback(demo);
    } else {
        ret = true;
    }
cleanup:
    if (available_instance_extensions) {
        free(available_instance_extensions);
    }
    if (enabled_extensions) {
        free(enabled_extensions);
    }

    return ret;
}

static bool create_surface(struct vulkan_demo *demo) {
    bool result;
    result =
        SDL_Vulkan_CreateSurface(demo->win, demo->instance, NULL, &demo->surface);
    if (!result) {
        err_log("creating vulkan surface failed: %s\n", SDL_GetError());
        return false;
    }
    return true;
}

static bool find_queue_families(VkPhysicalDevice physical_device, VkSurfaceKHR surface,
                         struct queue_family_indices *indices) {
    VkResult result;
    uint32_t queue_family_count = 0;
    uint32_t i = 0;
    bool ret = false;
    VkQueueFamilyProperties *queue_family_properties;
    VkBool32 present_support;

    vkGetPhysicalDeviceQueueFamilyProperties(physical_device,
                                             &queue_family_count, NULL);

    queue_family_properties =
        malloc(queue_family_count * sizeof(VkQueueFamilyProperties));
    vkGetPhysicalDeviceQueueFamilyProperties(
        physical_device, &queue_family_count, queue_family_properties);

    for (i = 0; i < queue_family_count; i++) {
        if (queue_family_properties[i].queueCount == 0) {
            continue;
        }
        if (queue_family_properties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            indices->graphics = i;
        }

        result = vkGetPhysicalDeviceSurfaceSupportKHR(
            physical_device, i, surface, &present_support);
        if (result != VK_SUCCESS) {
            err_log(
                    "vkGetPhysicalDeviceSurfaceSupportKHR failed with %d\n",
                    result);
            goto cleanup;
        }
        if (present_support == VK_TRUE) {
            indices->present = i;
        }
        if (indices->graphics >= 0 && indices->present >= 0) {
            break;
        }
    }
    ret = true;
cleanup:
    free(queue_family_properties);
    return ret;
}

static bool query_swap_chain_support(
    VkPhysicalDevice device, VkSurfaceKHR surface,
    struct swap_chain_support_details *swap_chain_support) {
    VkResult result;

    result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
        device, surface, &swap_chain_support->capabilities);
    if (result != VK_SUCCESS) {
        err_log(
                "vkGetPhysicalDeviceSurfaceCapabilitiesKHR failed: %d\n",
                result);
        return false;
    }

    result = vkGetPhysicalDeviceSurfaceFormatsKHR(
        device, surface, &swap_chain_support->formats_len, NULL);

    if (result != VK_SUCCESS) {
        err_log("vkGetPhysicalDeviceSurfaceFormatsKHR failed: %d\n",
                result);
        return false;
    }

    if (swap_chain_support->formats_len != 0) {
        swap_chain_support->formats = malloc(swap_chain_support->formats_len *
                                             sizeof(VkSurfaceFormatKHR));
        result = vkGetPhysicalDeviceSurfaceFormatsKHR(
            device, surface, &swap_chain_support->formats_len,
            swap_chain_support->formats);

        if (result != VK_SUCCESS) {
            err_log("vkGetPhysicalDeviceSurfaceFormatsKHR failed: %d\n",
                    result);
            return false;
        }
    }

    result = vkGetPhysicalDeviceSurfacePresentModesKHR(
        device, surface, &swap_chain_support->present_modes_len, NULL);

    if (result != VK_SUCCESS) {
        err_log(
                "vkGetPhysicalDeviceSurfacePresentModesKHR failed: %d\n",
                result);
        return false;
    }

    if (swap_chain_support->present_modes_len != 0) {
        swap_chain_support->present_modes = malloc(
            swap_chain_support->present_modes_len * sizeof(VkPresentModeKHR));
        result = vkGetPhysicalDeviceSurfacePresentModesKHR(
            device, surface, &swap_chain_support->present_modes_len,
            swap_chain_support->present_modes);

        if (result != VK_SUCCESS) {
            err_log(
                    "vkGetPhysicalDeviceSurfacePresentModesKHR failed: %d\n",
                    result);
            return false;
        }
    }

    return true;
}

enum PHY_DEV {
    PHY_DEV_NO,
    PHY_DEV_YES,
    PHY_DEV_PLACEBO,
};

static enum PHY_DEV is_suitable_physical_device(VkPhysicalDevice physical_device,
                                 VkSurfaceKHR surface,
                                 struct queue_family_indices *indices,
                                 VkPhysicalDeviceFeatures2 *physical_features2,
                                 VkPhysicalDeviceVulkan11Features *physical_features11,
                                 VkPhysicalDeviceVulkan12Features *physical_features12,
                                 VkPhysicalDeviceVulkan13Features *physical_features13,
                                 VkPhysicalDeviceDynamicRenderingFeatures *dyn_ren_features,
                                 VkPhysicalDeviceSynchronization2Features *syn2_features,
                                 VkPhysicalDevicePortabilitySubsetFeaturesKHR *port_sub_features,
                                 const char ***extensions,
                                 uint32_t *num_extensions,
                                 __attribute__((unused)) bool *use_metal_objects,
                                 __attribute__((unused)) bool *use_dynamic_rendering) {
    VkResult result;
    uint32_t device_extension_count;
    uint32_t i;
    VkExtensionProperties *device_extensions;
    enum PHY_DEV ret = PHY_DEV_NO;
    struct swap_chain_support_details swap_chain_support;
    int found_khr_surface = 0;
    int portability = 0;
    int synchronization2 = 0;
    int dynamic_rendering = 0;
    int metal_objects = 0;

    VkPhysicalDeviceProperties device_properties;
    vkGetPhysicalDeviceProperties(physical_device, &device_properties);

#ifndef NDEBUG
    err_log("Probing physical device %s\n", device_properties.deviceName);
#endif

    *extensions = NULL;
    *num_extensions = 0;

    if (device_properties.apiVersion < VK_MIN_VERSION) {
        err_log("vulkan version %d not available\n",
            VK_MIN_VERSION);
        return PHY_DEV_NO;
    }

    result = vkEnumerateDeviceExtensionProperties(
        physical_device, NULL, &device_extension_count, NULL);
    if (result != VK_SUCCESS) {
        err_log("vkEnumerateDeviceExtensionProperties failed: %d\n",
                result);
        return PHY_DEV_NO;
    }

    device_extensions =
        malloc(device_extension_count * sizeof(VkExtensionProperties));

    result = vkEnumerateDeviceExtensionProperties(
        physical_device, NULL, &device_extension_count, device_extensions);

    if (result != VK_SUCCESS) {
        err_log("vkEnumerateDeviceExtensionProperties failed: %d\n",
                result);
        goto cleanup;
    }

#ifndef NDEBUG
    err_log("  Supported device extensions:\n");
#endif

    for (i = 0; i < device_extension_count; i++) {
#ifndef NDEBUG
        err_log("    %s\n", device_extensions[i].extensionName);
#endif
        if (strcmp(VK_KHR_SWAPCHAIN_EXTENSION_NAME,
                   device_extensions[i].extensionName) == 0) {
            found_khr_surface = 1;
        }
        if (strcmp(VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME,
                   device_extensions[i].extensionName) == 0) {
            portability = 1;
        }
        if (strcmp(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME,
                   device_extensions[i].extensionName) == 0) {
            synchronization2 = 1;
        }
        if (strcmp(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
                   device_extensions[i].extensionName) == 0) {
            dynamic_rendering = 1;
        }
        if (strcmp(VK_EXT_METAL_OBJECTS_EXTENSION_NAME,
                   device_extensions[i].extensionName) == 0) {
            metal_objects = 1;
        }
    }
    if (!found_khr_surface) {
        err_log("  Device doesnt support %s\n", VK_KHR_SWAPCHAIN_EXTENSION_NAME);
        goto cleanup;
    }
    if (!find_queue_families(physical_device, surface, indices)) {
        goto cleanup;
    }
    if (indices->graphics < 0 || indices->present < 0) {
        err_log("  Device is missing graphics and/or present support. graphics: "
               "%d, present: %d\n",
               indices->graphics, indices->present);
        goto cleanup;
    }

    if (!query_swap_chain_support(physical_device, surface,
                                  &swap_chain_support)) {
        goto cleanup;
    }

    if (swap_chain_support.formats_len == 0) {
        err_log(" Device doesn't support any swap chain formats\n");
        goto cleanup;
    }

    if (swap_chain_support.present_modes_len == 0) {
        err_log(" Device doesn't support any swap chain present modes\n");
        goto cleanup;
    }
    ret = PHY_DEV_YES;

    *physical_features2 = (VkPhysicalDeviceFeatures2){ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
    *physical_features11 = (VkPhysicalDeviceVulkan11Features){ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES };
    *physical_features12 = (VkPhysicalDeviceVulkan12Features){ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
    *physical_features13 = (VkPhysicalDeviceVulkan13Features){ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };

    *dyn_ren_features = (VkPhysicalDeviceDynamicRenderingFeatures){ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES };
    *syn2_features = (VkPhysicalDeviceSynchronization2Features){ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES };
    *port_sub_features = (VkPhysicalDevicePortabilitySubsetFeaturesKHR){ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PORTABILITY_SUBSET_FEATURES_KHR };

    physical_features2->pNext = physical_features11;
    physical_features11->pNext = physical_features12;
#ifdef __APPLE__
    physical_features12->pNext = dyn_ren_features;
    dyn_ren_features->pNext = syn2_features;
    syn2_features->pNext = port_sub_features;
#else
    physical_features12->pNext = physical_features13;
    physical_features13->pNext = port_sub_features;
#endif

    vkGetPhysicalDeviceFeatures2(physical_device, physical_features2);
#ifdef __APPLE__
    if (device_properties.apiVersion < PL_VK_MIN_VERSION || !synchronization2 || !syn2_features->synchronization2) {
        goto cleanup;
    }
#else
    if (device_properties.apiVersion < PL_VK_MIN_VERSION || !synchronization2 || !physical_features13->synchronization2) {
        goto cleanup;
    }
#endif

    VkPhysicalDeviceFeatures *features = &physical_features2->features;

    int features_count = sizeof(VkPhysicalDeviceFeatures) / sizeof(VkBool32);
    VkBool32 *features_avail = (VkBool32 *)features;
    const VkBool32 *features_required = (const VkBool32 *)&pl_vulkan_required_features.features;
    const VkBool32 *features_rec = (const VkBool32 *)&pl_vulkan_recommended_features.features;

    for (int i = 0; i < features_count; ++i) {
        if (features_required[i] && !features_avail[i]) {
            err_log(" Device doesn't support required feature for libplacebo: %d\n", i);
            goto cleanup;
        }
        if (0 && features_avail[i]) {
            if (features_required[i] || features_rec[i]) {
                err_log("keeping feature %d\n", i);
                continue;
            }
            err_log("skipping feature %d\n", i);
            features_avail[i] = 0;
        }
    }

    struct vk_t {
        VkStructureType sType;
        void *pNext;
    } *vk_s;

#define CHECK_FEATURES(a, t, s, n) do { \
    int features_count = \
        (sizeof(t) - sizeof(struct vk_t)) / sizeof(VkBool32); \
 \
    features_avail = (VkBool32 *)((struct vk_t *)(a) + 1); \
 \
    vk_s = (struct vk_t *)&pl_vulkan_required_features; \
    while (vk_s && vk_s->sType != s) { \
        vk_s = vk_s->pNext; \
    } \
    features_required = vk_s ? (const VkBool32 *)(vk_s + 1) : NULL; \
 \
    vk_s = (struct vk_t *)&pl_vulkan_recommended_features; \
    while (vk_s && vk_s->sType != s) { \
        vk_s = vk_s->pNext; \
    } \
    features_rec = vk_s ? (const VkBool32 *)(vk_s + 1) : NULL; \
 \
    for (int i = 0; i < features_count; ++i) { \
        if (features_required && features_required[i] && !features_avail[i]) { \
            err_log(" Device doesn't support required " n " feature for libplacebo: %d\n", i); \
            goto cleanup; \
        } \
        if (0 && features_avail[i]) { \
            if ((features_required && features_required[i]) || (features_rec && features_rec[i])) { \
                err_log("keeping " n " feature %d\n", i); \
                continue; \
            } \
            err_log("skipping " n " feature %d\n", i); \
            features_avail[i] = 0; \
        } \
    } \
} while (0)

    CHECK_FEATURES(physical_features11, VkPhysicalDeviceVulkan11Features, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES, "Vulkan 1.1");
    CHECK_FEATURES(physical_features12, VkPhysicalDeviceVulkan12Features, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, "Vulkan 1.2");
    CHECK_FEATURES(physical_features13, VkPhysicalDeviceVulkan13Features, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, "Vulkan 1.3");

    ret = PHY_DEV_PLACEBO;
    for (int i = 0; i < pl_vulkan_num_recommended_extensions; ++i) {
        for (int j = 0; j < (int)device_extension_count; j++) {
            if (strcmp(pl_vulkan_recommended_extensions[i], device_extensions[j].extensionName) == 0) {
#ifndef NDEBUG
                err_log("%s available for libplacebo\n", pl_vulkan_recommended_extensions[i]);
#endif
#ifdef VK_EXT_full_screen_exclusive
                if (strcmp(pl_vulkan_recommended_extensions[i], VK_EXT_FULL_SCREEN_EXCLUSIVE_EXTENSION_NAME) == 0) {
                    continue;
                }
#endif
                ++*num_extensions;
            }
        }
    }

cleanup:
    *num_extensions += found_khr_surface + portability + synchronization2 + dynamic_rendering + metal_objects;
    *extensions = malloc(sizeof(const char *) * *num_extensions);
    int ext_i = 0;
    if (found_khr_surface) {
        (*extensions)[ext_i] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
        ++ext_i;
    }
    if (portability) {
        (*extensions)[ext_i] = VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME;
        ++ext_i;
    }
    if (synchronization2) {
        (*extensions)[ext_i] = VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME;
        ++ext_i;
    }
    if (dynamic_rendering) {
        (*extensions)[ext_i] = VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME;
        ++ext_i;
    }
    if (metal_objects) {
        (*extensions)[ext_i] = VK_EXT_METAL_OBJECTS_EXTENSION_NAME;
        ++ext_i;
    }
#ifdef __APPLE__
    *use_dynamic_rendering = 0 && dynamic_rendering && dyn_ren_features->dynamicRendering;
    *use_metal_objects = metal_objects;
#else
    *use_dynamic_rendering = 0 && dynamic_rendering && physical_features13->dynamicRendering;
#endif

    if (ret == PHY_DEV_PLACEBO) {
        for (int i = 0; i < pl_vulkan_num_recommended_extensions; ++i) {
            for (int j = 0; j < (int)device_extension_count; j++) {
                if (strcmp(pl_vulkan_recommended_extensions[i], device_extensions[j].extensionName) == 0) {
#ifdef VK_EXT_full_screen_exclusive
                    if (strcmp(pl_vulkan_recommended_extensions[i], VK_EXT_FULL_SCREEN_EXCLUSIVE_EXTENSION_NAME) == 0) {
                        continue;
                    }
#endif
                    (*extensions)[ext_i] = pl_vulkan_recommended_extensions[i];
                    ++ext_i;
                }
            }
        }
    }

    free(device_extensions);
    swap_chain_support_details_free(&swap_chain_support);

    return ret;
}

static bool create_physical_device(struct vulkan_demo *demo) {
    uint32_t device_count = 0;
    VkPhysicalDevice *physical_devices;
    VkResult result;
    uint32_t i;
    bool ret = false;

    result = vkEnumeratePhysicalDevices(demo->instance, &device_count, NULL);
    if (result != VK_SUCCESS) {
        err_log("vkEnumeratePhysicalDevices failed: %d\n", result);
        return ret;
    }
    if (device_count == 0) {
        err_log("no vulkan capable GPU found!");
        return ret;
    }

    physical_devices = malloc(device_count * sizeof(VkPhysicalDevice));
    result = vkEnumeratePhysicalDevices(demo->instance, &device_count,
                                        physical_devices);
    if (result != VK_SUCCESS) {
        err_log("vkEnumeratePhysicalDevices failed: %d\n", result);
        goto cleanup;
    }

    enum PHY_DEV phy_dev = PHY_DEV_NO;
    for (i = 0; i < device_count; i++) {
        struct queue_family_indices indices = {-1, -1};
        const char **extensions;
        uint32_t num_extensions;
        bool dynamic_rendering;
        bool metal_objects;
        enum PHY_DEV phy_dev_ret = is_suitable_physical_device(
            physical_devices[i], demo->surface,
            &indices,
            &demo->physical_features2,
            &demo->physical_features11,
            &demo->physical_features12,
            &demo->physical_features13,
            &demo->dyn_ren_features,
            &demo->syn2_features,
            &demo->port_sub_features,
            &extensions, &num_extensions, &metal_objects, &dynamic_rendering);
        if (phy_dev_ret > phy_dev) {
#ifndef NDEBUG
            err_log("  Selecting this device for rendering. Queue families: "
                   "graphics: %d, present: %d!\n",
                   indices.graphics, indices.present);
#endif
            if (demo->extensions) {
                free(demo->extensions);
            }
            demo->extensions = extensions;
            demo->num_extensions = num_extensions;
            demo->physical_device = physical_devices[i];
            demo->indices = indices;
            demo->dynamic_rendering = dynamic_rendering;
            demo->metal_objects = metal_objects;
            phy_dev = phy_dev_ret;
        }
        if (phy_dev == PHY_DEV_PLACEBO) {
#ifndef NDEBUG
            err_log("libplacebo available with the selected physical device.\n");
#endif
            break;
        }
    }
    if (demo->physical_device == NULL) {
        err_log("failed to find a suitable GPU!\n");
    } else {
        ret = true;
    }
    use_placebo = phy_dev == PHY_DEV_PLACEBO;
#ifdef __APPLE__
    use_rashader = demo->metal_objects;
#else
    use_rashader = 1;
#endif
cleanup:
    free(physical_devices);
    return ret;
}

static bool create_logical_device(struct vulkan_demo *demo) {
    VkResult result;
    bool ret = false;
    float queuePriority = 1.0f;
    uint32_t num_queues = 1;
    VkDeviceQueueCreateInfo *queue_create_infos;
    VkDeviceCreateInfo create_info;

    queue_create_infos = calloc(2, sizeof(VkDeviceQueueCreateInfo));
    queue_create_infos[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_create_infos[0].queueFamilyIndex = demo->indices.graphics;
    queue_create_infos[0].queueCount = 1;
    queue_create_infos[0].pQueuePriorities = &queuePriority;

    if (demo->indices.present != demo->indices.graphics) {
        queue_create_infos[1].sType =
            VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue_create_infos[1].queueFamilyIndex = demo->indices.present;
        queue_create_infos[1].queueCount = 1;
        queue_create_infos[1].pQueuePriorities = &queuePriority;
        num_queues = 2;
    }

    memset(&create_info, 0, sizeof(VkDeviceCreateInfo));
    create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    create_info.queueCreateInfoCount = num_queues;
    create_info.pQueueCreateInfos = queue_create_infos;
    create_info.enabledExtensionCount = demo->num_extensions;
    create_info.ppEnabledExtensionNames = demo->extensions;
    if (use_placebo || demo->dynamic_rendering) {
        create_info.pNext = &demo->physical_features2;
    }

    result = vkCreateDevice(demo->physical_device, &create_info, NULL,
                            &demo->device);

    if (result != VK_SUCCESS) {
        err_log("vkCreateDevice failed: %d\n", result);
        goto cleanup;
    }

    vkGetDeviceQueue(demo->device, demo->indices.graphics, 0,
                     &demo->graphics_queue);
    vkGetDeviceQueue(demo->device, demo->indices.present, 0,
                     &demo->present_queue);

#ifdef __APPLE__
    VkExportMetalObjectsInfoEXT mtl_ex_info = { VK_STRUCTURE_TYPE_EXPORT_METAL_OBJECTS_INFO_EXT };
    VkExportMetalCommandQueueInfoEXT mtl_queue_ex_info = { VK_STRUCTURE_TYPE_EXPORT_METAL_COMMAND_QUEUE_INFO_EXT };
    mtl_queue_ex_info.queue = demo->graphics_queue;
    mtl_ex_info.pNext = &mtl_queue_ex_info;
    vkExportMetalObjectsEXT(demo->device, &mtl_ex_info);
    demo->mtl_queue = mtl_queue_ex_info.mtlCommandQueue;
#endif

    ret = true;
cleanup:
    free(queue_create_infos);
    return ret;
}

static bool create_sampler(struct vulkan_demo *demo) {
    VkResult result;
    VkSamplerCreateInfo sampler_info;

    memset(&sampler_info, 0, sizeof(VkSamplerCreateInfo));
    sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_info.pNext = NULL;
    sampler_info.maxAnisotropy = 1.0;
    sampler_info.magFilter = VK_FILTER_LINEAR;
    sampler_info.minFilter = VK_FILTER_LINEAR;
    sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_info.mipLodBias = 0.0f;
    sampler_info.compareEnable = VK_FALSE;
    sampler_info.compareOp = VK_COMPARE_OP_ALWAYS;
    sampler_info.minLod = 0.0f;
    sampler_info.maxLod = 0.0f;
    sampler_info.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;

    result = vkCreateSampler(demo->device, &sampler_info, NULL, &demo->sampler);
    if (result != VK_SUCCESS) {
        err_log("vkCreateSampler failed: %d\n", result);
        return false;
    }
    return true;
}

static VkSurfaceFormatKHR choose_swap_surface_format(
    VkSurfaceFormatKHR *available_formats,
    uint32_t available_formats_len
) {
    VkSurfaceFormatKHR undefined_format = {VK_FORMAT_B8G8R8A8_UNORM,
                                           VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
    uint32_t i;
    if (available_formats_len == 1 &&
        available_formats[0].format == VK_FORMAT_UNDEFINED) {
        return undefined_format;
    }

    for (i = 0; i < available_formats_len; i++) {
        if (available_formats[i].format == VK_FORMAT_B8G8R8A8_UNORM &&
            available_formats[i].colorSpace ==
                VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return available_formats[i];
        }
    }

    return available_formats[0];
}

static VkPresentModeKHR choose_swap_present_mode(
    VkPresentModeKHR *available_present_modes,
    uint32_t available_present_modes_len
) {
    uint32_t i;
    for (i = 0; i < available_present_modes_len; i++) {
        /*
        best mode to ensure good input latency while ensuring we are not
        producing tearing
        */
        if (available_present_modes[i] == VK_PRESENT_MODE_MAILBOX_KHR) {
            return available_present_modes[i];
        }
    }
#ifdef __APPLE__
    for (i = 0; i < available_present_modes_len; i++) {
        if (available_present_modes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR) {
            return available_present_modes[i];
        }
    }
#endif

    /* must be supported */
    return VK_PRESENT_MODE_FIFO_KHR;
}

static VkExtent2D choose_swap_extent(
    struct vulkan_demo *demo,
    VkSurfaceCapabilitiesKHR *capabilities
) {
    VkExtent2D actual_extent;
    if (capabilities->currentExtent.width != 0xFFFFFFFF) {
        return capabilities->currentExtent;
    } else {
        actual_extent.width = demo->win_width_pixel;
        actual_extent.height = demo->win_height_pixel;

        actual_extent.width = NK_MAX(
            capabilities->minImageExtent.width,
            NK_MIN(capabilities->maxImageExtent.width, actual_extent.width));
        actual_extent.height = NK_MAX(
            capabilities->minImageExtent.height,
            NK_MIN(capabilities->maxImageExtent.height, actual_extent.height));

        return actual_extent;
    }
}

static bool create_swap_chain(struct vulkan_demo *demo) {
    struct swap_chain_support_details swap_chain_support;
    VkSurfaceFormatKHR surface_format;
    VkPresentModeKHR present_mode;
    VkExtent2D extent;
    VkResult result;
    VkSwapchainCreateInfoKHR create_info;
    uint32_t queue_family_indices[2];
    bool ret = false;

    queue_family_indices[0] = (uint32_t)demo->indices.graphics;
    queue_family_indices[1] = (uint32_t)demo->indices.present;

    if (!query_swap_chain_support(demo->physical_device, demo->surface,
                                  &swap_chain_support)) {
        goto cleanup;
    }
    surface_format = choose_swap_surface_format(swap_chain_support.formats,
                                                swap_chain_support.formats_len);
    present_mode = choose_swap_present_mode(
        swap_chain_support.present_modes, swap_chain_support.present_modes_len);
    if (present_mode != VK_PRESENT_MODE_MAILBOX_KHR) {
        // HACK to workaround pink border on macOS
        // https://github.com/libsdl-org/SDL/issues/7789
#if 0
        if (demo->resizing) {
            present_mode = VK_PRESENT_MODE_IMMEDIATE_KHR;
        } else {
            present_mode = VK_PRESENT_MODE_FIFO_KHR;
        }
#endif
    }
    extent = choose_swap_extent(demo, &swap_chain_support.capabilities);

    demo->swap_chain_images_len =
        swap_chain_support.capabilities.minImageCount + 1;
    if (swap_chain_support.capabilities.maxImageCount > 0 &&
        demo->swap_chain_images_len >
            swap_chain_support.capabilities.maxImageCount) {
        demo->swap_chain_images_len =
            swap_chain_support.capabilities.maxImageCount;
    }

    memset(&create_info, 0, sizeof(VkSwapchainCreateInfoKHR));
    create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    create_info.surface = demo->surface;

    create_info.minImageCount = demo->swap_chain_images_len;
    create_info.imageFormat = surface_format.format;
    create_info.imageColorSpace = surface_format.colorSpace;
    create_info.imageExtent = extent;
    create_info.imageArrayLayers = 1;
    create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    if (demo->indices.graphics != demo->indices.present) {
        create_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        create_info.queueFamilyIndexCount = 2;
        create_info.pQueueFamilyIndices = queue_family_indices;
    } else {
        create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    create_info.preTransform = swap_chain_support.capabilities.currentTransform;
    create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    create_info.presentMode = present_mode;
    create_info.clipped = VK_TRUE;

    result = vkCreateSwapchainKHR(demo->device, &create_info, NULL,
                                  &demo->swap_chain);

    if (result != VK_SUCCESS) {
        err_log("vkCreateSwapchainKHR failed: %d\n", result);
        goto cleanup;
    }

    result = vkGetSwapchainImagesKHR(demo->device, demo->swap_chain,
                                     &demo->swap_chain_images_len, NULL);
    if (result != VK_SUCCESS) {
        err_log("vkGetSwapchainImagesKHR failed: %d\n", result);
        goto cleanup;
    }
    if (demo->swap_chain_images == NULL) {
        demo->swap_chain_images =
            malloc(demo->swap_chain_images_len * sizeof(VkImage));
    }
    result = vkGetSwapchainImagesKHR(demo->device, demo->swap_chain,
                                     &demo->swap_chain_images_len,
                                     demo->swap_chain_images);

    if (result != VK_SUCCESS) {
        err_log("vkGetSwapchainImagesKHR failed: %d\n", result);
        return false;
    }

    demo->swap_chain_image_format = surface_format.format;
    demo->swap_chain_image_extent = extent;

    ret = true;
cleanup:
    swap_chain_support_details_free(&swap_chain_support);

    return ret;
}

static bool create_swap_chain_image_views(struct vulkan_demo *demo) {
    uint32_t i;
    VkResult result;
    VkImageViewCreateInfo create_info;

    memset(&create_info, 0, sizeof(VkImageViewCreateInfo));
    create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    create_info.format = demo->swap_chain_image_format;
    create_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    create_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    create_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    create_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    create_info.subresourceRange.baseMipLevel = 0;
    create_info.subresourceRange.levelCount = 1;
    create_info.subresourceRange.baseArrayLayer = 0;
    create_info.subresourceRange.layerCount = 1;

    if (!demo->swap_chain_image_views) {
        demo->swap_chain_image_views =
            malloc(demo->swap_chain_images_len * sizeof(VkImageView));
    }

    for (i = 0; i < demo->swap_chain_images_len; i++) {
        create_info.image = demo->swap_chain_images[i];
        result = vkCreateImageView(demo->device, &create_info, NULL,
                                   &demo->swap_chain_image_views[i]);

        if (result != VK_SUCCESS) {
            err_log("vkCreateImageView failed: %d\n", result);
            return false;
        }
    }
    return true;
}

static bool create_overlay_images(struct vulkan_demo *demo) {
    uint32_t i, j;
    VkResult result;
    VkMemoryRequirements mem_requirements;
    VkPhysicalDeviceMemoryProperties mem_properties;
    int found;
    VkImageCreateInfo image_info;
    VkMemoryAllocateInfo alloc_info;
    VkImageViewCreateInfo image_view_info;

    memset(&image_info, 0, sizeof(VkImageCreateInfo));
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.extent.width = demo->swap_chain_image_extent.width;
    image_info.extent.height = demo->swap_chain_image_extent.height;
    image_info.extent.depth = 1;
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.format = demo->swap_chain_image_format;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    image_info.usage =
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    memset(&alloc_info, 0, sizeof(VkMemoryAllocateInfo));
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;

    memset(&image_view_info, 0, sizeof(VkImageViewCreateInfo));
    image_view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    image_view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    image_view_info.format = demo->swap_chain_image_format;
    image_view_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    image_view_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    image_view_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    image_view_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    image_view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    image_view_info.subresourceRange.baseMipLevel = 0;
    image_view_info.subresourceRange.levelCount = 1;
    image_view_info.subresourceRange.baseArrayLayer = 0;
    image_view_info.subresourceRange.layerCount = 1;

    if (!demo->overlay_images) {
        demo->overlay_images =
            malloc(demo->swap_chain_images_len * sizeof(VkImage));
    }

    if (!demo->overlay_image_memories) {
        demo->overlay_image_memories =
            malloc(demo->swap_chain_images_len * sizeof(VkDeviceMemory));
    }
    if (!demo->overlay_image_views) {
        demo->overlay_image_views =
            malloc(demo->swap_chain_images_len * sizeof(VkImageView));
    }

    for (i = 0; i < demo->swap_chain_images_len; i++) {
        result = vkCreateImage(demo->device, &image_info, NULL,
                               &demo->overlay_images[i]);

        if (result != VK_SUCCESS) {
            err_log("vkCreateImage failed for index %lu: %d\n",
                    (unsigned long)i, result);
            return false;
        }

        vkGetImageMemoryRequirements(demo->device, demo->overlay_images[i],
                                     &mem_requirements);

        alloc_info.allocationSize = mem_requirements.size;

        vkGetPhysicalDeviceMemoryProperties(demo->physical_device,
                                            &mem_properties);
        found = 0;
        for (j = 0; j < mem_properties.memoryTypeCount; j++) {
            if ((mem_requirements.memoryTypeBits & (1 << j)) &&
                (mem_properties.memoryTypes[j].propertyFlags &
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) ==
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) {
                found = 1;
                break;
            }
        }
        if (!found) {
            err_log(
                    "failed to find suitable memory type for index %lu!\n",
                    (unsigned long)i);
            return false;
        }
        alloc_info.memoryTypeIndex = j;
        result = vkAllocateMemory(demo->device, &alloc_info, NULL,
                                  &demo->overlay_image_memories[i]);
        if (result != VK_SUCCESS) {
            err_log(
                    "failed to allocate vulkan memory for index %lu: %d!\n",
                    (unsigned long)i, result);
            return false;
        }
        result = vkBindImageMemory(demo->device, demo->overlay_images[i],
                                   demo->overlay_image_memories[i], 0);
        if (result != VK_SUCCESS) {
            err_log("Couldn't bind image memory for index %lu: %d\n",
                    (unsigned long)i, result);
            return false;
        }

        image_view_info.image = demo->overlay_images[i];
        result = vkCreateImageView(demo->device, &image_view_info, NULL,
                                   &demo->overlay_image_views[i]);

        if (result != VK_SUCCESS) {
            err_log("vkCreateImageView failed for index %lu: %d\n",
                    (unsigned long)i, result);
            return false;
        }
    }
    return true;
}

static bool create_render_pass(struct vulkan_demo *demo) {
    VkAttachmentDescription attachment;
    VkAttachmentReference color_attachment_ref;
    VkSubpassDescription subpass;
    VkSubpassDependency dependency;
    VkRenderPassCreateInfo render_pass_info;
    VkResult result;

    memset(&attachment, 0, sizeof(VkAttachmentDescription));
    attachment.format = demo->swap_chain_image_format;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    memset(&color_attachment_ref, 0, sizeof(VkAttachmentReference));
    color_attachment_ref.attachment = 0;
    color_attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    memset(&subpass, 0, sizeof(VkSubpassDescription));
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_attachment_ref;

    memset(&dependency, 0, sizeof(VkSubpassDependency));
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                               VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    memset(&render_pass_info, 0, sizeof(VkRenderPassCreateInfo));
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    render_pass_info.attachmentCount = 1;
    render_pass_info.pAttachments = &attachment;
    render_pass_info.subpassCount = 1;
    render_pass_info.pSubpasses = &subpass;
    render_pass_info.dependencyCount = 1;
    render_pass_info.pDependencies = &dependency;

    result = vkCreateRenderPass(demo->device, &render_pass_info, NULL,
                                &demo->render_pass);
    if (result != VK_SUCCESS) {
        err_log("vkCreateRenderPass failed: %d\n", result);
        return false;
    }

    attachment.format = VK_FORMAT;
    attachment.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

    dependency.srcSubpass = 0;
    dependency.dstSubpass = VK_SUBPASS_EXTERNAL;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
    dependency.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

    result = vkCreateRenderPass(demo->device, &render_pass_info, NULL,
                                &demo->cursor_render_pass);
    if (result != VK_SUCCESS) {
        err_log("vkCreateRenderPass cursor failed: %d\n", result);
        return false;
    }

    return true;
}

static bool create_framebuffers(struct vulkan_demo *demo) {
    uint32_t i;
    VkResult result;
    VkFramebufferCreateInfo framebuffer_info;

    if (!demo->framebuffers) {
        demo->framebuffers =
            malloc(demo->swap_chain_images_len * sizeof(VkFramebuffer));
    }

    memset(&framebuffer_info, 0, sizeof(VkFramebufferCreateInfo));
    framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebuffer_info.renderPass = demo->render_pass;
    framebuffer_info.attachmentCount = 1;
    framebuffer_info.width = demo->swap_chain_image_extent.width;
    framebuffer_info.height = demo->swap_chain_image_extent.height;
    framebuffer_info.layers = 1;

    for (i = 0; i < demo->swap_chain_images_len; i++) {
        framebuffer_info.pAttachments = &demo->swap_chain_image_views[i];

        result = vkCreateFramebuffer(demo->device, &framebuffer_info, NULL,
                                     &demo->framebuffers[i]);
        if (result != VK_SUCCESS) {
            err_log("vkCreateFramebuffer failed from index %lu: %d\n",
                    (unsigned long)i, result);
            return false;
        }
    }
    return true;
}

static bool create_descriptor_set_layout(struct vulkan_demo *demo) {
    VkDescriptorSetLayoutBinding overlay_layout_binding;
    VkDescriptorSetLayoutCreateInfo descriptor_set_layout_create_nfo;
    VkResult result;

    memset(&overlay_layout_binding, 0, sizeof(VkDescriptorSetLayoutBinding));
    overlay_layout_binding.binding = 0;
    overlay_layout_binding.descriptorCount = 1;
    overlay_layout_binding.descriptorType =
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    overlay_layout_binding.pImmutableSamplers = NULL;
    overlay_layout_binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    memset(&descriptor_set_layout_create_nfo, 0,
           sizeof(VkDescriptorSetLayoutCreateInfo));
    descriptor_set_layout_create_nfo.sType =
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descriptor_set_layout_create_nfo.bindingCount = 1;
    descriptor_set_layout_create_nfo.pBindings = &overlay_layout_binding;

    result = vkCreateDescriptorSetLayout(demo->device,
                                         &descriptor_set_layout_create_nfo,
                                         NULL, &demo->descriptor_set_layout);
    if (result != VK_SUCCESS) {
        err_log("vkCreateDescriptorSetLayout failed: %d\n", result);
        return false;
    }
    return true;
}

static bool create_descriptor_pool(struct vulkan_demo *demo) {
    VkDescriptorPoolSize pool_size;
    VkDescriptorPoolCreateInfo pool_info;
    VkResult result;

    memset(&pool_size, 0, sizeof(VkDescriptorPoolSize));
    pool_size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    pool_size.descriptorCount = demo->swap_chain_images_len;

    memset(&pool_info, 0, sizeof(VkDescriptorPoolCreateInfo));
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.poolSizeCount = 1;
    pool_info.pPoolSizes = &pool_size;
    pool_info.maxSets = demo->swap_chain_images_len + VK_VIEW_DESC_COUNT_MAX;
    result = vkCreateDescriptorPool(demo->device, &pool_info, NULL,
                                    &demo->descriptor_pool);

    if (result != VK_SUCCESS) {
        err_log("vkCreateDescriptorPool failed: %d\n", result);
        return false;
    }
    return true;
}

static void update_descriptor_sets(struct vulkan_demo *demo) {
    uint32_t i;
    VkDescriptorImageInfo descriptor_image_info;
    VkWriteDescriptorSet descriptor_write;

    memset(&descriptor_image_info, 0, sizeof(VkDescriptorImageInfo));
    descriptor_image_info.imageLayout =
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    descriptor_image_info.sampler = demo->sampler;

    memset(&descriptor_write, 0, sizeof(VkWriteDescriptorSet));
    descriptor_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptor_write.dstBinding = 0;
    descriptor_write.dstArrayElement = 0;
    descriptor_write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptor_write.descriptorCount = 1;
    descriptor_write.pImageInfo = &descriptor_image_info;

    for (i = 0; i < demo->swap_chain_images_len; i++) {
        descriptor_write.dstSet = demo->descriptor_sets[i];
        descriptor_image_info.imageView = demo->overlay_image_views[i];

        vkUpdateDescriptorSets(demo->device, 1, &descriptor_write, 0, NULL);
    }
}

static bool create_descriptor_sets(struct vulkan_demo *demo) {
    bool ret = false;
    VkDescriptorSetLayout *descriptor_set_layouts;
    VkDescriptorSetAllocateInfo alloc_info;
    uint32_t i;
    VkResult result;

    demo->descriptor_sets =
        malloc(demo->swap_chain_images_len * sizeof(VkDescriptorSet));
    descriptor_set_layouts =
        malloc(demo->swap_chain_images_len * sizeof(VkDescriptorSetLayout));

    for (i = 0; i < demo->swap_chain_images_len; i++) {
        descriptor_set_layouts[i] = demo->descriptor_set_layout;
    }

    memset(&alloc_info, 0, sizeof(VkDescriptorSetAllocateInfo));
    alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.descriptorPool = demo->descriptor_pool;
    alloc_info.descriptorSetCount = demo->swap_chain_images_len;
    alloc_info.pSetLayouts = descriptor_set_layouts;
    result = vkAllocateDescriptorSets(demo->device, &alloc_info,
                                      demo->descriptor_sets);
    if (result != VK_SUCCESS) {
        err_log("vkAllocateDescriptorSets failed: %d\n", result);
        goto cleanup;
    }

    update_descriptor_sets(demo);

    ret = true;
cleanup:
    free(descriptor_set_layouts);

    return ret;
}

static bool create_shader_module(
    VkDevice device, const char *shader_buffer,
    size_t shader_buffer_len,
    VkShaderModule *shader_module
) {
    VkShaderModuleCreateInfo create_info;
    VkResult result;

    memset(&create_info, 0, sizeof(VkShaderModuleCreateInfo));
    create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    create_info.codeSize = shader_buffer_len;
    create_info.pCode = (const uint32_t *)shader_buffer;

    result = vkCreateShaderModule(device, &create_info, NULL, shader_module);
    if (result != VK_SUCCESS) {
        err_log("vkCreateShaderModule failed: %d\n", result);
        return false;
    }

    return true;
}

static const char shaders_demo_frag_spv[] = {
    0x03, 0x02, 0x23, 0x07, 0x00, 0x00, 0x01, 0x00, 0x0b, 0x00, 0x0d, 0x00,
    0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x11, 0x00, 0x02, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x06, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x47, 0x4c, 0x53, 0x4c, 0x2e, 0x73, 0x74, 0x64, 0x2e, 0x34, 0x35, 0x30,
    0x00, 0x00, 0x00, 0x00, 0x0e, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x07, 0x00, 0x04, 0x00, 0x00, 0x00,
    0x04, 0x00, 0x00, 0x00, 0x6d, 0x61, 0x69, 0x6e, 0x00, 0x00, 0x00, 0x00,
    0x09, 0x00, 0x00, 0x00, 0x11, 0x00, 0x00, 0x00, 0x10, 0x00, 0x03, 0x00,
    0x04, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x03, 0x00, 0x03, 0x00,
    0x02, 0x00, 0x00, 0x00, 0xc2, 0x01, 0x00, 0x00, 0x04, 0x00, 0x09, 0x00,
    0x47, 0x4c, 0x5f, 0x41, 0x52, 0x42, 0x5f, 0x73, 0x65, 0x70, 0x61, 0x72,
    0x61, 0x74, 0x65, 0x5f, 0x73, 0x68, 0x61, 0x64, 0x65, 0x72, 0x5f, 0x6f,
    0x62, 0x6a, 0x65, 0x63, 0x74, 0x73, 0x00, 0x00, 0x04, 0x00, 0x0a, 0x00,
    0x47, 0x4c, 0x5f, 0x47, 0x4f, 0x4f, 0x47, 0x4c, 0x45, 0x5f, 0x63, 0x70,
    0x70, 0x5f, 0x73, 0x74, 0x79, 0x6c, 0x65, 0x5f, 0x6c, 0x69, 0x6e, 0x65,
    0x5f, 0x64, 0x69, 0x72, 0x65, 0x63, 0x74, 0x69, 0x76, 0x65, 0x00, 0x00,
    0x04, 0x00, 0x08, 0x00, 0x47, 0x4c, 0x5f, 0x47, 0x4f, 0x4f, 0x47, 0x4c,
    0x45, 0x5f, 0x69, 0x6e, 0x63, 0x6c, 0x75, 0x64, 0x65, 0x5f, 0x64, 0x69,
    0x72, 0x65, 0x63, 0x74, 0x69, 0x76, 0x65, 0x00, 0x05, 0x00, 0x04, 0x00,
    0x04, 0x00, 0x00, 0x00, 0x6d, 0x61, 0x69, 0x6e, 0x00, 0x00, 0x00, 0x00,
    0x05, 0x00, 0x05, 0x00, 0x09, 0x00, 0x00, 0x00, 0x6f, 0x75, 0x74, 0x43,
    0x6f, 0x6c, 0x6f, 0x72, 0x00, 0x00, 0x00, 0x00, 0x05, 0x00, 0x04, 0x00,
    0x0d, 0x00, 0x00, 0x00, 0x6f, 0x76, 0x65, 0x72, 0x6c, 0x61, 0x79, 0x00,
    0x05, 0x00, 0x04, 0x00, 0x11, 0x00, 0x00, 0x00, 0x69, 0x6e, 0x55, 0x56,
    0x00, 0x00, 0x00, 0x00, 0x47, 0x00, 0x04, 0x00, 0x09, 0x00, 0x00, 0x00,
    0x1e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x47, 0x00, 0x04, 0x00,
    0x0d, 0x00, 0x00, 0x00, 0x21, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x47, 0x00, 0x04, 0x00, 0x0d, 0x00, 0x00, 0x00, 0x22, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x47, 0x00, 0x04, 0x00, 0x11, 0x00, 0x00, 0x00,
    0x1e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x13, 0x00, 0x02, 0x00,
    0x02, 0x00, 0x00, 0x00, 0x21, 0x00, 0x03, 0x00, 0x03, 0x00, 0x00, 0x00,
    0x02, 0x00, 0x00, 0x00, 0x16, 0x00, 0x03, 0x00, 0x06, 0x00, 0x00, 0x00,
    0x20, 0x00, 0x00, 0x00, 0x17, 0x00, 0x04, 0x00, 0x07, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x20, 0x00, 0x04, 0x00,
    0x08, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00,
    0x3b, 0x00, 0x04, 0x00, 0x08, 0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00,
    0x03, 0x00, 0x00, 0x00, 0x19, 0x00, 0x09, 0x00, 0x0a, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x1b, 0x00, 0x03, 0x00, 0x0b, 0x00, 0x00, 0x00,
    0x0a, 0x00, 0x00, 0x00, 0x20, 0x00, 0x04, 0x00, 0x0c, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x3b, 0x00, 0x04, 0x00,
    0x0c, 0x00, 0x00, 0x00, 0x0d, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x17, 0x00, 0x04, 0x00, 0x0f, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00,
    0x02, 0x00, 0x00, 0x00, 0x20, 0x00, 0x04, 0x00, 0x10, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x00, 0x3b, 0x00, 0x04, 0x00,
    0x10, 0x00, 0x00, 0x00, 0x11, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x36, 0x00, 0x05, 0x00, 0x02, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0xf8, 0x00, 0x02, 0x00,
    0x05, 0x00, 0x00, 0x00, 0x3d, 0x00, 0x04, 0x00, 0x0b, 0x00, 0x00, 0x00,
    0x0e, 0x00, 0x00, 0x00, 0x0d, 0x00, 0x00, 0x00, 0x3d, 0x00, 0x04, 0x00,
    0x0f, 0x00, 0x00, 0x00, 0x12, 0x00, 0x00, 0x00, 0x11, 0x00, 0x00, 0x00,
    0x57, 0x00, 0x05, 0x00, 0x07, 0x00, 0x00, 0x00, 0x13, 0x00, 0x00, 0x00,
    0x0e, 0x00, 0x00, 0x00, 0x12, 0x00, 0x00, 0x00, 0x3e, 0x00, 0x03, 0x00,
    0x09, 0x00, 0x00, 0x00, 0x13, 0x00, 0x00, 0x00, 0xfd, 0x00, 0x01, 0x00,
    0x38, 0x00, 0x01, 0x00
};
static const unsigned int shaders_demo_frag_spv_len = 664;

static const char shaders_demo_data_frag_spv[] = {
    0x03, 0x02, 0x23, 0x07, 0x00, 0x00, 0x01, 0x00, 0x0b, 0x00, 0x0d, 0x00,
    0x1d, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x11, 0x00, 0x02, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x06, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x47, 0x4c, 0x53, 0x4c, 0x2e, 0x73, 0x74, 0x64, 0x2e, 0x34, 0x35, 0x30,
    0x00, 0x00, 0x00, 0x00, 0x0e, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x07, 0x00, 0x04, 0x00, 0x00, 0x00,
    0x04, 0x00, 0x00, 0x00, 0x6d, 0x61, 0x69, 0x6e, 0x00, 0x00, 0x00, 0x00,
    0x09, 0x00, 0x00, 0x00, 0x11, 0x00, 0x00, 0x00, 0x10, 0x00, 0x03, 0x00,
    0x04, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x03, 0x00, 0x03, 0x00,
    0x02, 0x00, 0x00, 0x00, 0xc2, 0x01, 0x00, 0x00, 0x04, 0x00, 0x09, 0x00,
    0x47, 0x4c, 0x5f, 0x41, 0x52, 0x42, 0x5f, 0x73, 0x65, 0x70, 0x61, 0x72,
    0x61, 0x74, 0x65, 0x5f, 0x73, 0x68, 0x61, 0x64, 0x65, 0x72, 0x5f, 0x6f,
    0x62, 0x6a, 0x65, 0x63, 0x74, 0x73, 0x00, 0x00, 0x04, 0x00, 0x0a, 0x00,
    0x47, 0x4c, 0x5f, 0x47, 0x4f, 0x4f, 0x47, 0x4c, 0x45, 0x5f, 0x63, 0x70,
    0x70, 0x5f, 0x73, 0x74, 0x79, 0x6c, 0x65, 0x5f, 0x6c, 0x69, 0x6e, 0x65,
    0x5f, 0x64, 0x69, 0x72, 0x65, 0x63, 0x74, 0x69, 0x76, 0x65, 0x00, 0x00,
    0x04, 0x00, 0x08, 0x00, 0x47, 0x4c, 0x5f, 0x47, 0x4f, 0x4f, 0x47, 0x4c,
    0x45, 0x5f, 0x69, 0x6e, 0x63, 0x6c, 0x75, 0x64, 0x65, 0x5f, 0x64, 0x69,
    0x72, 0x65, 0x63, 0x74, 0x69, 0x76, 0x65, 0x00, 0x05, 0x00, 0x04, 0x00,
    0x04, 0x00, 0x00, 0x00, 0x6d, 0x61, 0x69, 0x6e, 0x00, 0x00, 0x00, 0x00,
    0x05, 0x00, 0x05, 0x00, 0x09, 0x00, 0x00, 0x00, 0x6f, 0x75, 0x74, 0x43,
    0x6f, 0x6c, 0x6f, 0x72, 0x00, 0x00, 0x00, 0x00, 0x05, 0x00, 0x04, 0x00,
    0x0d, 0x00, 0x00, 0x00, 0x6f, 0x76, 0x65, 0x72, 0x6c, 0x61, 0x79, 0x00,
    0x05, 0x00, 0x04, 0x00, 0x11, 0x00, 0x00, 0x00, 0x69, 0x6e, 0x55, 0x56,
    0x00, 0x00, 0x00, 0x00, 0x47, 0x00, 0x04, 0x00, 0x09, 0x00, 0x00, 0x00,
    0x1e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x47, 0x00, 0x04, 0x00,
    0x0d, 0x00, 0x00, 0x00, 0x21, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x47, 0x00, 0x04, 0x00, 0x0d, 0x00, 0x00, 0x00, 0x22, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x47, 0x00, 0x04, 0x00, 0x11, 0x00, 0x00, 0x00,
    0x1e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x13, 0x00, 0x02, 0x00,
    0x02, 0x00, 0x00, 0x00, 0x21, 0x00, 0x03, 0x00, 0x03, 0x00, 0x00, 0x00,
    0x02, 0x00, 0x00, 0x00, 0x16, 0x00, 0x03, 0x00, 0x06, 0x00, 0x00, 0x00,
    0x20, 0x00, 0x00, 0x00, 0x17, 0x00, 0x04, 0x00, 0x07, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x20, 0x00, 0x04, 0x00,
    0x08, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00,
    0x3b, 0x00, 0x04, 0x00, 0x08, 0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00,
    0x03, 0x00, 0x00, 0x00, 0x19, 0x00, 0x09, 0x00, 0x0a, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x1b, 0x00, 0x03, 0x00, 0x0b, 0x00, 0x00, 0x00,
    0x0a, 0x00, 0x00, 0x00, 0x20, 0x00, 0x04, 0x00, 0x0c, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x3b, 0x00, 0x04, 0x00,
    0x0c, 0x00, 0x00, 0x00, 0x0d, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x17, 0x00, 0x04, 0x00, 0x0f, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00,
    0x02, 0x00, 0x00, 0x00, 0x20, 0x00, 0x04, 0x00, 0x10, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x00, 0x3b, 0x00, 0x04, 0x00,
    0x10, 0x00, 0x00, 0x00, 0x11, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x15, 0x00, 0x04, 0x00, 0x12, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x2b, 0x00, 0x04, 0x00, 0x12, 0x00, 0x00, 0x00,
    0x13, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x20, 0x00, 0x04, 0x00,
    0x14, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00,
    0x2b, 0x00, 0x04, 0x00, 0x12, 0x00, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x36, 0x00, 0x05, 0x00, 0x02, 0x00, 0x00, 0x00,
    0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
    0xf8, 0x00, 0x02, 0x00, 0x05, 0x00, 0x00, 0x00, 0x3d, 0x00, 0x04, 0x00,
    0x0b, 0x00, 0x00, 0x00, 0x0e, 0x00, 0x00, 0x00, 0x0d, 0x00, 0x00, 0x00,
    0x41, 0x00, 0x05, 0x00, 0x14, 0x00, 0x00, 0x00, 0x15, 0x00, 0x00, 0x00,
    0x11, 0x00, 0x00, 0x00, 0x13, 0x00, 0x00, 0x00, 0x3d, 0x00, 0x04, 0x00,
    0x06, 0x00, 0x00, 0x00, 0x16, 0x00, 0x00, 0x00, 0x15, 0x00, 0x00, 0x00,
    0x7f, 0x00, 0x04, 0x00, 0x06, 0x00, 0x00, 0x00, 0x17, 0x00, 0x00, 0x00,
    0x16, 0x00, 0x00, 0x00, 0x41, 0x00, 0x05, 0x00, 0x14, 0x00, 0x00, 0x00,
    0x19, 0x00, 0x00, 0x00, 0x11, 0x00, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00,
    0x3d, 0x00, 0x04, 0x00, 0x06, 0x00, 0x00, 0x00, 0x1a, 0x00, 0x00, 0x00,
    0x19, 0x00, 0x00, 0x00, 0x50, 0x00, 0x05, 0x00, 0x0f, 0x00, 0x00, 0x00,
    0x1b, 0x00, 0x00, 0x00, 0x17, 0x00, 0x00, 0x00, 0x1a, 0x00, 0x00, 0x00,
    0x57, 0x00, 0x05, 0x00, 0x07, 0x00, 0x00, 0x00, 0x1c, 0x00, 0x00, 0x00,
    0x0e, 0x00, 0x00, 0x00, 0x1b, 0x00, 0x00, 0x00, 0x3e, 0x00, 0x03, 0x00,
    0x09, 0x00, 0x00, 0x00, 0x1c, 0x00, 0x00, 0x00, 0xfd, 0x00, 0x01, 0x00,
    0x38, 0x00, 0x01, 0x00
};
static const unsigned int shaders_demo_data_frag_spv_len = 820;

static const char shaders_demo_vert_spv[] = {
    0x03, 0x02, 0x23, 0x07, 0x00, 0x00, 0x01, 0x00, 0x0b, 0x00, 0x0d, 0x00,
    0x2c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x11, 0x00, 0x02, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x06, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x47, 0x4c, 0x53, 0x4c, 0x2e, 0x73, 0x74, 0x64, 0x2e, 0x34, 0x35, 0x30,
    0x00, 0x00, 0x00, 0x00, 0x0e, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x04, 0x00, 0x00, 0x00, 0x6d, 0x61, 0x69, 0x6e, 0x00, 0x00, 0x00, 0x00,
    0x09, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x00, 0x1d, 0x00, 0x00, 0x00,
    0x03, 0x00, 0x03, 0x00, 0x02, 0x00, 0x00, 0x00, 0xc2, 0x01, 0x00, 0x00,
    0x04, 0x00, 0x09, 0x00, 0x47, 0x4c, 0x5f, 0x41, 0x52, 0x42, 0x5f, 0x73,
    0x65, 0x70, 0x61, 0x72, 0x61, 0x74, 0x65, 0x5f, 0x73, 0x68, 0x61, 0x64,
    0x65, 0x72, 0x5f, 0x6f, 0x62, 0x6a, 0x65, 0x63, 0x74, 0x73, 0x00, 0x00,
    0x04, 0x00, 0x0a, 0x00, 0x47, 0x4c, 0x5f, 0x47, 0x4f, 0x4f, 0x47, 0x4c,
    0x45, 0x5f, 0x63, 0x70, 0x70, 0x5f, 0x73, 0x74, 0x79, 0x6c, 0x65, 0x5f,
    0x6c, 0x69, 0x6e, 0x65, 0x5f, 0x64, 0x69, 0x72, 0x65, 0x63, 0x74, 0x69,
    0x76, 0x65, 0x00, 0x00, 0x04, 0x00, 0x08, 0x00, 0x47, 0x4c, 0x5f, 0x47,
    0x4f, 0x4f, 0x47, 0x4c, 0x45, 0x5f, 0x69, 0x6e, 0x63, 0x6c, 0x75, 0x64,
    0x65, 0x5f, 0x64, 0x69, 0x72, 0x65, 0x63, 0x74, 0x69, 0x76, 0x65, 0x00,
    0x05, 0x00, 0x04, 0x00, 0x04, 0x00, 0x00, 0x00, 0x6d, 0x61, 0x69, 0x6e,
    0x00, 0x00, 0x00, 0x00, 0x05, 0x00, 0x04, 0x00, 0x09, 0x00, 0x00, 0x00,
    0x6f, 0x75, 0x74, 0x55, 0x56, 0x00, 0x00, 0x00, 0x05, 0x00, 0x06, 0x00,
    0x0c, 0x00, 0x00, 0x00, 0x67, 0x6c, 0x5f, 0x56, 0x65, 0x72, 0x74, 0x65,
    0x78, 0x49, 0x6e, 0x64, 0x65, 0x78, 0x00, 0x00, 0x05, 0x00, 0x06, 0x00,
    0x1b, 0x00, 0x00, 0x00, 0x67, 0x6c, 0x5f, 0x50, 0x65, 0x72, 0x56, 0x65,
    0x72, 0x74, 0x65, 0x78, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0x06, 0x00,
    0x1b, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x67, 0x6c, 0x5f, 0x50,
    0x6f, 0x73, 0x69, 0x74, 0x69, 0x6f, 0x6e, 0x00, 0x06, 0x00, 0x07, 0x00,
    0x1b, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x67, 0x6c, 0x5f, 0x50,
    0x6f, 0x69, 0x6e, 0x74, 0x53, 0x69, 0x7a, 0x65, 0x00, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x07, 0x00, 0x1b, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
    0x67, 0x6c, 0x5f, 0x43, 0x6c, 0x69, 0x70, 0x44, 0x69, 0x73, 0x74, 0x61,
    0x6e, 0x63, 0x65, 0x00, 0x06, 0x00, 0x07, 0x00, 0x1b, 0x00, 0x00, 0x00,
    0x03, 0x00, 0x00, 0x00, 0x67, 0x6c, 0x5f, 0x43, 0x75, 0x6c, 0x6c, 0x44,
    0x69, 0x73, 0x74, 0x61, 0x6e, 0x63, 0x65, 0x00, 0x05, 0x00, 0x03, 0x00,
    0x1d, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x47, 0x00, 0x04, 0x00,
    0x09, 0x00, 0x00, 0x00, 0x1e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x47, 0x00, 0x04, 0x00, 0x0c, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00,
    0x2a, 0x00, 0x00, 0x00, 0x47, 0x00, 0x03, 0x00, 0x1b, 0x00, 0x00, 0x00,
    0x02, 0x00, 0x00, 0x00, 0x48, 0x00, 0x05, 0x00, 0x1b, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x48, 0x00, 0x05, 0x00, 0x1b, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x0b, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x48, 0x00, 0x05, 0x00,
    0x1b, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00,
    0x03, 0x00, 0x00, 0x00, 0x48, 0x00, 0x05, 0x00, 0x1b, 0x00, 0x00, 0x00,
    0x03, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00,
    0x13, 0x00, 0x02, 0x00, 0x02, 0x00, 0x00, 0x00, 0x21, 0x00, 0x03, 0x00,
    0x03, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x16, 0x00, 0x03, 0x00,
    0x06, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x17, 0x00, 0x04, 0x00,
    0x07, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
    0x20, 0x00, 0x04, 0x00, 0x08, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
    0x07, 0x00, 0x00, 0x00, 0x3b, 0x00, 0x04, 0x00, 0x08, 0x00, 0x00, 0x00,
    0x09, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x15, 0x00, 0x04, 0x00,
    0x0a, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x20, 0x00, 0x04, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x0a, 0x00, 0x00, 0x00, 0x3b, 0x00, 0x04, 0x00, 0x0b, 0x00, 0x00, 0x00,
    0x0c, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x2b, 0x00, 0x04, 0x00,
    0x0a, 0x00, 0x00, 0x00, 0x0e, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x2b, 0x00, 0x04, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00,
    0x02, 0x00, 0x00, 0x00, 0x17, 0x00, 0x04, 0x00, 0x17, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x15, 0x00, 0x04, 0x00,
    0x18, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x2b, 0x00, 0x04, 0x00, 0x18, 0x00, 0x00, 0x00, 0x19, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x1c, 0x00, 0x04, 0x00, 0x1a, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x00, 0x00, 0x19, 0x00, 0x00, 0x00, 0x1e, 0x00, 0x06, 0x00,
    0x1b, 0x00, 0x00, 0x00, 0x17, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00,
    0x1a, 0x00, 0x00, 0x00, 0x1a, 0x00, 0x00, 0x00, 0x20, 0x00, 0x04, 0x00,
    0x1c, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x1b, 0x00, 0x00, 0x00,
    0x3b, 0x00, 0x04, 0x00, 0x1c, 0x00, 0x00, 0x00, 0x1d, 0x00, 0x00, 0x00,
    0x03, 0x00, 0x00, 0x00, 0x2b, 0x00, 0x04, 0x00, 0x0a, 0x00, 0x00, 0x00,
    0x1e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x2b, 0x00, 0x04, 0x00,
    0x06, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40,
    0x2b, 0x00, 0x04, 0x00, 0x06, 0x00, 0x00, 0x00, 0x22, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x80, 0xbf, 0x2b, 0x00, 0x04, 0x00, 0x06, 0x00, 0x00, 0x00,
    0x25, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x2b, 0x00, 0x04, 0x00,
    0x06, 0x00, 0x00, 0x00, 0x26, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x3f,
    0x20, 0x00, 0x04, 0x00, 0x2a, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
    0x17, 0x00, 0x00, 0x00, 0x36, 0x00, 0x05, 0x00, 0x02, 0x00, 0x00, 0x00,
    0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
    0xf8, 0x00, 0x02, 0x00, 0x05, 0x00, 0x00, 0x00, 0x3d, 0x00, 0x04, 0x00,
    0x0a, 0x00, 0x00, 0x00, 0x0d, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x00,
    0xc4, 0x00, 0x05, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x00,
    0x0d, 0x00, 0x00, 0x00, 0x0e, 0x00, 0x00, 0x00, 0xc7, 0x00, 0x05, 0x00,
    0x0a, 0x00, 0x00, 0x00, 0x11, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x00,
    0x10, 0x00, 0x00, 0x00, 0x6f, 0x00, 0x04, 0x00, 0x06, 0x00, 0x00, 0x00,
    0x12, 0x00, 0x00, 0x00, 0x11, 0x00, 0x00, 0x00, 0x3d, 0x00, 0x04, 0x00,
    0x0a, 0x00, 0x00, 0x00, 0x13, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x00,
    0xc7, 0x00, 0x05, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x14, 0x00, 0x00, 0x00,
    0x13, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x6f, 0x00, 0x04, 0x00,
    0x06, 0x00, 0x00, 0x00, 0x15, 0x00, 0x00, 0x00, 0x14, 0x00, 0x00, 0x00,
    0x50, 0x00, 0x05, 0x00, 0x07, 0x00, 0x00, 0x00, 0x16, 0x00, 0x00, 0x00,
    0x12, 0x00, 0x00, 0x00, 0x15, 0x00, 0x00, 0x00, 0x3e, 0x00, 0x03, 0x00,
    0x09, 0x00, 0x00, 0x00, 0x16, 0x00, 0x00, 0x00, 0x3d, 0x00, 0x04, 0x00,
    0x07, 0x00, 0x00, 0x00, 0x1f, 0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00,
    0x8e, 0x00, 0x05, 0x00, 0x07, 0x00, 0x00, 0x00, 0x21, 0x00, 0x00, 0x00,
    0x1f, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x50, 0x00, 0x05, 0x00,
    0x07, 0x00, 0x00, 0x00, 0x23, 0x00, 0x00, 0x00, 0x22, 0x00, 0x00, 0x00,
    0x22, 0x00, 0x00, 0x00, 0x81, 0x00, 0x05, 0x00, 0x07, 0x00, 0x00, 0x00,
    0x24, 0x00, 0x00, 0x00, 0x21, 0x00, 0x00, 0x00, 0x23, 0x00, 0x00, 0x00,
    0x51, 0x00, 0x05, 0x00, 0x06, 0x00, 0x00, 0x00, 0x27, 0x00, 0x00, 0x00,
    0x24, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x51, 0x00, 0x05, 0x00,
    0x06, 0x00, 0x00, 0x00, 0x28, 0x00, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x50, 0x00, 0x07, 0x00, 0x17, 0x00, 0x00, 0x00,
    0x29, 0x00, 0x00, 0x00, 0x27, 0x00, 0x00, 0x00, 0x28, 0x00, 0x00, 0x00,
    0x25, 0x00, 0x00, 0x00, 0x26, 0x00, 0x00, 0x00, 0x41, 0x00, 0x05, 0x00,
    0x2a, 0x00, 0x00, 0x00, 0x2b, 0x00, 0x00, 0x00, 0x1d, 0x00, 0x00, 0x00,
    0x1e, 0x00, 0x00, 0x00, 0x3e, 0x00, 0x03, 0x00, 0x2b, 0x00, 0x00, 0x00,
    0x29, 0x00, 0x00, 0x00, 0xfd, 0x00, 0x01, 0x00, 0x38, 0x00, 0x01, 0x00
};
static const unsigned int shaders_demo_vert_spv_len = 1284;

static bool create_graphics_pipeline(struct vulkan_demo *demo) {
    bool ret = false;
    VkShaderModule vert_shader_module = VK_NULL_HANDLE;
    VkShaderModule frag_shader_module = VK_NULL_HANDLE;
    VkShaderModule data_frag_shader_module = VK_NULL_HANDLE;
    VkPipelineShaderStageCreateInfo vert_shader_stage_info;
    VkPipelineShaderStageCreateInfo frag_shader_stage_info;
    VkPipelineShaderStageCreateInfo shader_stages[2];
    VkPipelineVertexInputStateCreateInfo vertex_input_info;
    VkPipelineInputAssemblyStateCreateInfo input_assembly;
    VkViewport viewport;
    VkRect2D scissor;
    VkPipelineViewportStateCreateInfo viewport_state;
    VkPipelineRasterizationStateCreateInfo rasterizer;
    VkPipelineMultisampleStateCreateInfo multisampling;
    VkPipelineColorBlendAttachmentState color_blend_attachment;
    VkPipelineColorBlendStateCreateInfo color_blending;
    VkPipelineLayoutCreateInfo pipeline_layout_info;
    VkResult result;
    VkGraphicsPipelineCreateInfo pipeline_info;
    VkPipelineDynamicStateCreateInfo pipeline_dyn_state_info;

    if (!create_shader_module(demo->device, shaders_demo_vert_spv, shaders_demo_vert_spv_len,
                              &vert_shader_module)) {
        goto cleanup;
    }

    if (!create_shader_module(demo->device, shaders_demo_frag_spv, shaders_demo_frag_spv_len,
                              &frag_shader_module)) {
        goto cleanup;
    }

    if (!create_shader_module(demo->device, shaders_demo_data_frag_spv, shaders_demo_data_frag_spv_len,
                              &data_frag_shader_module)) {
        goto cleanup;
    }

    memset(&vert_shader_stage_info, 0, sizeof(VkPipelineShaderStageCreateInfo));
    vert_shader_stage_info.sType =
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vert_shader_stage_info.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vert_shader_stage_info.module = vert_shader_module;
    vert_shader_stage_info.pName = "main";

    memset(&frag_shader_stage_info, 0, sizeof(VkPipelineShaderStageCreateInfo));
    frag_shader_stage_info.sType =
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    frag_shader_stage_info.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    frag_shader_stage_info.module = frag_shader_module;
    frag_shader_stage_info.pName = "main";

    shader_stages[0] = vert_shader_stage_info;
    shader_stages[1] = frag_shader_stage_info;

    memset(&vertex_input_info, 0, sizeof(VkPipelineVertexInputStateCreateInfo));
    vertex_input_info.sType =
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    memset(&input_assembly, 0, sizeof(VkPipelineInputAssemblyStateCreateInfo));
    input_assembly.sType =
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    input_assembly.primitiveRestartEnable = VK_FALSE;

    memset(&viewport, 0, sizeof(VkViewport));
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float)demo->swap_chain_image_extent.width;
    viewport.height = (float)demo->swap_chain_image_extent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    memset(&scissor, 0, sizeof(VkRect2D));
    scissor.extent.width = demo->swap_chain_image_extent.width;
    scissor.extent.height = demo->swap_chain_image_extent.height;

    memset(&viewport_state, 0, sizeof(VkPipelineViewportStateCreateInfo));
    viewport_state.sType =
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.pViewports = &viewport;
    viewport_state.scissorCount = 1;
    viewport_state.pScissors = &scissor;

    memset(&rasterizer, 0, sizeof(VkPipelineRasterizationStateCreateInfo));
    rasterizer.sType =
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_FRONT_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    memset(&multisampling, 0, sizeof(VkPipelineMultisampleStateCreateInfo));
    multisampling.sType =
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    memset(&color_blend_attachment, 0,
           sizeof(VkPipelineColorBlendAttachmentState));
    color_blend_attachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    color_blend_attachment.blendEnable = VK_TRUE;
    color_blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    color_blend_attachment.dstColorBlendFactor =
        VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    color_blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
    color_blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    color_blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    color_blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;

    memset(&color_blending, 0, sizeof(VkPipelineColorBlendStateCreateInfo));
    color_blending.sType =
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blending.logicOpEnable = VK_FALSE;
    color_blending.logicOp = VK_LOGIC_OP_COPY;
    color_blending.attachmentCount = 1;
    color_blending.pAttachments = &color_blend_attachment;
    color_blending.blendConstants[0] = 1.0f;
    color_blending.blendConstants[1] = 1.0f;
    color_blending.blendConstants[2] = 1.0f;
    color_blending.blendConstants[3] = 1.0f;

    memset(&pipeline_layout_info, 0, sizeof(VkPipelineLayoutCreateInfo));
    pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_layout_info.setLayoutCount = 0;
    pipeline_layout_info.pushConstantRangeCount = 0;
    pipeline_layout_info.setLayoutCount = 1;
    pipeline_layout_info.pSetLayouts = &demo->descriptor_set_layout;

    result = vkCreatePipelineLayout(demo->device, &pipeline_layout_info, NULL,
                                    &demo->pipeline_layout);

    if (result != VK_SUCCESS) {
        err_log("vkCreatePipelineLayout failed: %d\n", result);
        goto cleanup;
    }

    VkDynamicState dyn_state[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    memset(&pipeline_dyn_state_info, 0, sizeof(VkPipelineDynamicStateCreateInfo));
    pipeline_dyn_state_info.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    pipeline_dyn_state_info.pNext = NULL;
    pipeline_dyn_state_info.flags = 0;
    pipeline_dyn_state_info.dynamicStateCount = sizeof(dyn_state) / sizeof(*dyn_state);
    pipeline_dyn_state_info.pDynamicStates = dyn_state;

    memset(&pipeline_info, 0, sizeof(VkGraphicsPipelineCreateInfo));
    pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_info.stageCount = 2;
    pipeline_info.pStages = shader_stages;
    pipeline_info.pVertexInputState = &vertex_input_info;
    pipeline_info.pInputAssemblyState = &input_assembly;
    pipeline_info.pViewportState = &viewport_state;
    pipeline_info.pRasterizationState = &rasterizer;
    pipeline_info.pMultisampleState = &multisampling;
    pipeline_info.pColorBlendState = &color_blending;
    pipeline_info.pDynamicState = &pipeline_dyn_state_info;
    pipeline_info.layout = demo->pipeline_layout;
    pipeline_info.renderPass = demo->render_pass;
    pipeline_info.basePipelineHandle = NULL;

    result = vkCreateGraphicsPipelines(demo->device, NULL, 1, &pipeline_info,
                                       NULL, &demo->pipeline);
    if (result != VK_SUCCESS) {
        err_log("vkCreateGraphicsPipelines failed: %d\n", result);
        goto cleanup;
    }

    shader_stages[1].module = data_frag_shader_module;

    result = vkCreateGraphicsPipelines(demo->device, NULL, 1, &pipeline_info,
                                       NULL, &demo->data_pipeline);
    if (result != VK_SUCCESS) {
        err_log("vkCreateGraphicsPipelines failed: %d\n", result);
        goto cleanup;
    }

    shader_stages[1].module = frag_shader_module;
    pipeline_info.renderPass = demo->cursor_render_pass;

    result = vkCreateGraphicsPipelines(demo->device, NULL, 1, &pipeline_info,
                                       NULL, &demo->cursor_pipeline);
    if (result != VK_SUCCESS) {
        err_log("vkCreateGraphicsPipelines failed: %d\n", result);
        goto cleanup;
    }

    ret = true;
cleanup:
    if (data_frag_shader_module) {
        vkDestroyShaderModule(demo->device, data_frag_shader_module, NULL);
    }
    if (frag_shader_module) {
        vkDestroyShaderModule(demo->device, frag_shader_module, NULL);
    }
    if (vert_shader_module) {
        vkDestroyShaderModule(demo->device, vert_shader_module, NULL);
    }
    return ret;
}

static bool create_command_pool(struct vulkan_demo *demo) {
    VkCommandPoolCreateInfo pool_info;
    VkResult result;

    memset(&pool_info, 0, sizeof(VkCommandPoolCreateInfo));
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = demo->indices.graphics;

    result = vkCreateCommandPool(demo->device, &pool_info, NULL,
                                 &demo->command_pool);
    if (result != VK_SUCCESS) {
        err_log("vkCreateCommandPool failed: %d\n", result);
        return false;
    }
    return true;
}

static bool create_command_buffers(struct vulkan_demo *demo) {
    VkCommandBufferAllocateInfo alloc_info;
    VkResult result;

    demo->command_buffers =
        malloc(demo->swap_chain_images_len * sizeof(VkCommandBuffer));

    for (int i = 0; i < SCREEN_COUNT; ++i) {
        demo->upload_command_buffers[i] =
            malloc(demo->swap_chain_images_len * sizeof(VkCommandBuffer));
        demo->libra_command_buffers[i] =
            malloc(demo->swap_chain_images_len * sizeof(VkCommandBuffer));
    }

    memset(&alloc_info, 0, sizeof(VkCommandBufferAllocateInfo));
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.commandPool = demo->command_pool;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = demo->swap_chain_images_len;

    result = vkAllocateCommandBuffers(demo->device, &alloc_info,
                                      demo->command_buffers);
    if (result != VK_SUCCESS) {
        err_log("vkAllocateCommandBuffers failed: %d\n", result);
        return false;
    }

    for (int i = 0; i < SCREEN_COUNT; ++i) {
        result = vkAllocateCommandBuffers(
            demo->device, &alloc_info,
            demo->upload_command_buffers[i]);
        if (result != VK_SUCCESS) {
            err_log("vkAllocateCommandBuffers failed: %d\n", result);
            return false;
        }

        result = vkAllocateCommandBuffers(
            demo->device, &alloc_info,
            demo->libra_command_buffers[i]);
        if (result != VK_SUCCESS) {
            err_log("vkAllocateCommandBuffers failed: %d\n", result);
            return false;
        }
    }

    return true;
}

static bool create_semaphores(struct vulkan_demo *demo) {
    VkSemaphoreCreateInfo semaphore_info;
    VkResult result;

    memset(&semaphore_info, 0, sizeof(VkSemaphoreCreateInfo));
    semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    result = vkCreateSemaphore(demo->device, &semaphore_info, NULL,
                               &demo->image_available);
    if (result != VK_SUCCESS) {
        err_log("vkCreateSemaphore failed: %d\n", result);
        return false;
    }
    result = vkCreateSemaphore(demo->device, &semaphore_info, NULL,
                               &demo->render_finished);
    if (result != VK_SUCCESS) {
        err_log("vkCreateSemaphore failed: %d\n", result);
        return false;
    }
#ifdef __APPLE__
    VkExportMetalObjectCreateInfoEXT mtl_ex_info = { VK_STRUCTURE_TYPE_EXPORT_METAL_OBJECT_CREATE_INFO_EXT };
    mtl_ex_info.exportObjectType = VK_EXPORT_METAL_OBJECT_TYPE_METAL_SHARED_EVENT_BIT_EXT;
    semaphore_info.pNext = &mtl_ex_info;

    VkSemaphoreTypeCreateInfo tl_info = { VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO };
    tl_info.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    mtl_ex_info.pNext = &tl_info;
#endif
    for (int i = 0; i < SCREEN_COUNT; ++i) {
        result = vkCreateSemaphore(demo->device, &semaphore_info, NULL,
            &demo->upload_sem[i]);
        if (result != VK_SUCCESS) {
            err_log("vkCreateSemaphore failed: %d\n", result);
            return false;
        }
        result = vkCreateSemaphore(demo->device, &semaphore_info, NULL,
            &demo->libra_sem[i]);
        if (result != VK_SUCCESS) {
            err_log("vkCreateSemaphore failed: %d\n", result);
            return false;
        }
#ifdef __APPLE__
        VkExportMetalObjectsInfoEXT ex_info = { VK_STRUCTURE_TYPE_EXPORT_METAL_OBJECTS_INFO_EXT };
        VkExportMetalSharedEventInfoEXT evt_ex_info = { VK_STRUCTURE_TYPE_EXPORT_METAL_SHARED_EVENT_INFO_EXT };
        ex_info.pNext = &evt_ex_info;

        evt_ex_info.semaphore = demo->upload_sem[i];
        vkExportMetalObjectsEXT(demo->device, &ex_info);
        demo->upload_evt[i] = evt_ex_info.mtlSharedEvent;
        demo->upload_val[i] = 1;

        evt_ex_info.semaphore = demo->libra_sem[i];
        vkExportMetalObjectsEXT(demo->device, &ex_info);
        demo->libra_evt[i] = evt_ex_info.mtlSharedEvent;
        demo->libra_val[i] = 1;
#endif
    }
    return true;
}

static bool create_fence(struct vulkan_demo *demo) {
    VkResult result;
    VkFenceCreateInfo fence_create_info;

    memset(&fence_create_info, 0, sizeof(VkFenceCreateInfo));
    fence_create_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_create_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    result = vkCreateFence(demo->device, &fence_create_info, NULL,
                           &demo->render_fence);

    if (result != VK_SUCCESS) {
        err_log("vkCreateFence failed: %d\n", result);
        return false;
    }
    return true;
}

static bool create_swap_chain_related_resources(struct vulkan_demo *demo) {
    if (!create_swap_chain(demo)) {
        return false;
    }
    if (!create_swap_chain_image_views(demo)) {
        return false;
    }
    if (!create_overlay_images(demo)) {
        return false;
    }
    if (!create_render_pass(demo)) {
        return false;
    }
    if (!create_framebuffers(demo)) {
        return false;
    }
    if (!create_graphics_pipeline(demo)) {
        return false;
    }
    return true;
}

static bool destroy_swap_chain_related_resources(struct vulkan_demo *demo) {
    uint32_t i;
    VkResult result;

    result = vkQueueWaitIdle(demo->graphics_queue);
    if (result != VK_SUCCESS) {
        err_log("vkQueueWaitIdle failed: %d\n", result);
        return false;
    }

    for (i = 0; i < demo->swap_chain_images_len; i++) {
        vkDestroyFramebuffer(demo->device, demo->framebuffers[i], NULL);
        vkDestroyImageView(demo->device, demo->overlay_image_views[i], NULL);
        vkDestroyImage(demo->device, demo->overlay_images[i], NULL);
        vkFreeMemory(demo->device, demo->overlay_image_memories[i], NULL);
        vkDestroyImageView(demo->device, demo->swap_chain_image_views[i], NULL);
    }
    vkDestroySwapchainKHR(demo->device, demo->swap_chain, NULL);
    vkDestroyRenderPass(demo->device, demo->render_pass, NULL);
    vkDestroyRenderPass(demo->device, demo->cursor_render_pass, NULL);
    vkDestroyPipeline(demo->device, demo->pipeline, NULL);
    vkDestroyPipeline(demo->device, demo->data_pipeline, NULL);
    vkDestroyPipeline(demo->device, demo->cursor_pipeline, NULL);
    vkDestroyPipelineLayout(demo->device, demo->pipeline_layout, NULL);
    return true;
}

static bool create_vulkan_demo(struct vulkan_demo *demo) {
    if (!create_surface(demo)) {
        return false;
    }
    if (!create_physical_device(demo)) {
        return false;
    }
    if (!create_logical_device(demo)) {
        return false;
    }
    if (!create_sampler(demo)) {
        return false;
    }
    if (!create_descriptor_set_layout(demo)) {
        return false;
    }
    if (!create_swap_chain_related_resources(demo)) {
        return false;
    }
    if (!create_descriptor_pool(demo)) {
        return false;
    }
    if (!create_descriptor_sets(demo)) {
        return false;
    }
    if (!create_command_pool(demo)) {
        return false;
    }
    if (!create_command_buffers(demo)) {
        return false;
    }
    if (!create_semaphores(demo)) {
        return false;
    }
    if (!create_fence(demo)) {
        return false;
    }

    return true;
}

static bool recreate_swap_chain(struct vulkan_demo *demo, bool nk) {
    if (!destroy_swap_chain_related_resources(demo)) {
        return false;
    }
    if (!create_swap_chain_related_resources(demo)) {
        return false;
    }

    update_descriptor_sets(demo);
    if (nk)
        nk_sdl_vk_resize(demo->win_scale,
                         demo->swap_chain_image_extent.width,
                         demo->swap_chain_image_extent.height);

    return true;
}

static VkResult destroy_debug_utils_messenger_ext(
    VkInstance instance,
    VkDebugUtilsMessengerEXT debugMessenger,
    const VkAllocationCallbacks *pAllocator
) {
    PFN_vkDestroyDebugUtilsMessengerEXT func =
        (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
            instance, "vkDestroyDebugUtilsMessengerEXT");
    if (func != NULL) {
        func(instance, debugMessenger, pAllocator);
        return VK_SUCCESS;
    } else {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
}

static void destroy_vulkan_demo(struct vulkan_demo *demo) {
    VkResult result;

#ifndef NDEBUG
    err_log("cleaning up\n");
#endif
    result = vkDeviceWaitIdle(demo->device);
    if (result != VK_SUCCESS) {
        err_log("vkDeviceWaitIdle failed: %d\n", result);
    }

    destroy_swap_chain_related_resources(demo);

    vkFreeCommandBuffers(demo->device, demo->command_pool,
                         demo->swap_chain_images_len, demo->command_buffers);
    for(int i = 0; i < SCREEN_COUNT; ++i) {
        vkFreeCommandBuffers(demo->device, demo->command_pool,
            demo->swap_chain_images_len, demo->upload_command_buffers[i]);
        vkFreeCommandBuffers(demo->device, demo->command_pool,
            demo->swap_chain_images_len, demo->libra_command_buffers[i]);
    }
    vkDestroyCommandPool(demo->device, demo->command_pool, NULL);
    vkDestroySampler(demo->device, demo->sampler, NULL);
    vkDestroySemaphore(demo->device, demo->render_finished, NULL);
    vkDestroySemaphore(demo->device, demo->image_available, NULL);
    for (int i = 0; i < SCREEN_COUNT; ++i) {
        vkDestroySemaphore(demo->device, demo->upload_sem[i], NULL);
        vkDestroySemaphore(demo->device, demo->libra_sem[i], NULL);
    }
    vkDestroyFence(demo->device, demo->render_fence, NULL);

    vkDestroyDescriptorSetLayout(demo->device, demo->descriptor_set_layout,
                                 NULL);
    vkDestroyDescriptorPool(demo->device, demo->descriptor_pool, NULL);

    vkDestroyDevice(demo->device, NULL);
    vkDestroySurfaceKHR(demo->instance, demo->surface, NULL);

    if (demo->extensions) {
        free(demo->extensions);
    }
    if (demo->swap_chain_images) {
        free(demo->swap_chain_images);
    }
    if (demo->swap_chain_image_views) {
        free(demo->swap_chain_image_views);
    }

    if (demo->overlay_images) {
        free(demo->overlay_images);
    }
    if (demo->overlay_image_views) {
        free(demo->overlay_image_views);
    }
    if (demo->overlay_image_memories) {
        free(demo->overlay_image_memories);
    }

    if (demo->descriptor_sets) {
        free(demo->descriptor_sets);
    }
    if (demo->framebuffers) {
        free(demo->framebuffers);
    }
    if (demo->command_buffers) {
        free(demo->command_buffers);
    }
    for (int i = 0; i < SCREEN_COUNT; ++i) {
        if (demo->upload_command_buffers[i]) {
            free(demo->upload_command_buffers[i]);
        }
        if (demo->libra_command_buffers[i]) {
            free(demo->libra_command_buffers[i]);
        }
    }

    use_placebo = false;
}

static void destroy_instance(struct vulkan_demo *demo) {
    VkResult result;

    if (demo->debug_messenger) {
        result = destroy_debug_utils_messenger_ext(demo->instance,
                                                   demo->debug_messenger, NULL);
        if (result != VK_SUCCESS) {
            err_log("Couldn't destroy debug messenger: %d\n", result);
        }
    }
    vkDestroyInstance(demo->instance, NULL);
}

static struct vulkan_demo vk_demo[SCREEN_COUNT];
static SDL_Window *sdl_win[SCREEN_COUNT];
static struct nk_context *nk_ctx;
static VmaAllocator vma[SCREEN_COUNT];

enum {
    UPSCALING_DEFAULT_NONE = 0,
    UPSCALING_DEFAULT_COUNT,
};

#define PLACEBO_UI_INDEX(mode) (UPSCALING_DEFAULT_COUNT + mode)
#define PLACEBO_MODE(ui_index) (ui_index - PLACEBO_UI_INDEX(0))
#define IS_PLACEBO(ui_index) (PLACEBO_MODE(ui_index) >= 0 && PLACEBO_MODE(ui_index) < placebo_count)

static struct placebo_t *placebo;
static int placebo_count;
static struct placebo_render_t *placebo_render[SCREEN_COUNT][SCREEN_COUNT];
static int placebo_render_mode[SCREEN_COUNT][SCREEN_COUNT];
static VkSemaphore placebo_in_sem[SCREEN_COUNT][SCREEN_COUNT];
static VkSemaphore placebo_sem[SCREEN_COUNT][SCREEN_COUNT];
#ifdef __APPLE__
static uint64_t placebo_in_val[SCREEN_COUNT][SCREEN_COUNT];
static uint64_t placebo_val[SCREEN_COUNT][SCREEN_COUNT];
#endif

static pl_vulkan pl_vk_dev[SCREEN_COUNT];
static pl_log pl_log_dev;

#define RASHADER_UI_INDEX(mode) (UPSCALING_DEFAULT_COUNT + placebo_count + mode)
#define RASHADER_MODE(ui_index) (ui_index - RASHADER_UI_INDEX(0))
#define IS_RASHADER(ui_index) (RASHADER_MODE(ui_index) >= 0 && RASHADER_MODE(ui_index) < rashader_count)

static struct rashader_t *rashader;
static int rashader_count;
static struct rashader_render_t *rashader_render[SCREEN_COUNT][SCREEN_COUNT];
static int rashader_render_mode[SCREEN_COUNT][SCREEN_COUNT];

static int vk_upscaling_init(void) {
    ui_upscaling_filter_count = UPSCALING_DEFAULT_COUNT;

    pl_log_dev = placebo_log_create();
    for (int j = 0; j < SCREEN_COUNT; ++j) {
        pl_vk_dev[j] = pl_vulkan_import(pl_log_dev, pl_vulkan_import_params(
            .instance = vk_demo[j].instance,
            .phys_device = vk_demo[j].physical_device,
            .device = vk_demo[j].device,
            .get_proc_addr = vkGetInstanceProcAddr,
            .queue_graphics = { vk_demo[j].indices.graphics, 1 },
            .features = &vk_demo[j].physical_features2,
            .extensions = vk_demo[j].extensions,
            .num_extensions = vk_demo[j].num_extensions,
        ));
        if (!pl_vk_dev[j]) {
            use_placebo = false;
        }

        for (int i = 0; i < SCREEN_COUNT; ++i) {
            placebo_render_mode[j][i] = -1;
            rashader_render_mode[j][i] = -1;

            placebo_in_sem[j][i] = pl_vulkan_sem_create(pl_vk_dev[j]->gpu, pl_vulkan_sem_params(
#ifdef __APPLE__
                .type = VK_SEMAPHORE_TYPE_TIMELINE
#endif
            ));
            placebo_sem[j][i] = pl_vulkan_sem_create(pl_vk_dev[j]->gpu, pl_vulkan_sem_params(
#ifdef __APPLE__
                .type = VK_SEMAPHORE_TYPE_TIMELINE
#endif
            ));
            if (!placebo_in_sem[j][i] || !placebo_sem[j][i]) {
                use_placebo = false;
            }
#ifdef __APPLE__
            placebo_in_val[j][i] = placebo_val[j][i] = 1;
#endif
        }
    }

    if (use_placebo) {
        placebo = placebo_load("placebo.json");
        if (placebo) {
            placebo_count = placebo_mode_count(placebo);
            ui_upscaling_filter_count += placebo_count;
        }
    }

    if (use_rashader) {
        rashader = rashader_load("rashader.json");
        if (rashader) {
            rashader_count = rashader_mode_count(rashader);
            ui_upscaling_filter_count += rashader_count;
        }
    }

    ui_upscaling_filter_options = malloc(ui_upscaling_filter_count * sizeof(*ui_upscaling_filter_options));
    if (!ui_upscaling_filter_options) {
        return -1;
    }

    ui_upscaling_filter_options[UPSCALING_DEFAULT_NONE] = NK_UPSCALE_TYPE_TEXT_NONE "None";

    for (int i = 0; i < placebo_count; ++i) {
        ui_upscaling_filter_options[PLACEBO_UI_INDEX(i)] = placebo_mode_name(placebo, i, NK_UPSCALE_TYPE_TEXT_PLACEBO);
    }

    for (int i = 0; i < rashader_count; ++i) {
        ui_upscaling_filter_options[RASHADER_UI_INDEX(i)] = rashader_mode_name(rashader, i, NK_UPSCALE_TYPE_TEXT_RASHADER);
    }

    ui_upscaling_selected = UPSCALING_DEFAULT_NONE;

    return 0;
}

#ifndef __APPLE__
static void vk_filter_chain_free(void *, void *);
#endif

static void vk_upscaling_close(void) {
    for (int j = 0; j < SCREEN_COUNT; ++j) {
        pl_gpu_finish(pl_vk_dev[j]->gpu);
        for (int i = 0; i < SCREEN_COUNT; ++i) {
            pl_vulkan_sem_destroy(pl_vk_dev[j]->gpu, &placebo_sem[j][i]);
            pl_vulkan_sem_destroy(pl_vk_dev[j]->gpu, &placebo_in_sem[j][i]);
        }

        for (int i = 0; i < SCREEN_COUNT; ++i) {
            if (placebo_render[j][i]) {
                placebo_render_close(placebo_render[j][i]);
                placebo_render[j][i] = 0;
            }
        }

        for (int i = 0; i < SCREEN_COUNT; ++i) {
            if (rashader_render[j][i]) {
#ifdef __APPLE__
                rashader_render_close(rashader_render[j][i], mtl_filter_chain_free, NULL);
#else
                rashader_render_close(rashader_render[j][i], vk_filter_chain_free, NULL);
#endif
                rashader_render[j][i] = 0;
            }
        }

        pl_vulkan_destroy(&pl_vk_dev[j]);
    }
    placebo_log_destroy(&pl_log_dev);

    if (placebo) {
        placebo_unload(placebo);
        placebo = 0;
    }
    placebo_count = 0;

    if (rashader) {
        rashader_unload(rashader);
        rashader = 0;
    }
    rashader_count = 0;

    if (ui_upscaling_filter_options) {
        free(ui_upscaling_filter_options);
        ui_upscaling_filter_options = 0;
    }
    ui_upscaling_filter_count = 0;
}

static int placebo_upscaling_update(int selected, int ctx_top_bot, int screen_top_bot) {
    int i = ctx_top_bot;

    int render_mode = -1;
    bool reset_mode = 0;
    if (selected >= 0) {
        render_mode = selected;
    } else {
        reset_mode = 1;
    }

    if (
        placebo_render[i][screen_top_bot] && (
            placebo_render_mode[i][screen_top_bot] != render_mode ||
            reset_mode
        )
    ) {
        placebo_render_close(placebo_render[i][screen_top_bot]);
        placebo_render[i][screen_top_bot] = 0;
    }

    if (!reset_mode && !placebo_render[i][screen_top_bot] && render_mode >= 0 && placebo) {
        placebo_render[i][screen_top_bot] = placebo_render_init(placebo, render_mode, pl_vk_dev[i]->gpu, pl_log_dev);
        if (!placebo_render[i][screen_top_bot]) {
            err_log("placebo_render_init failed\n");
            goto fail;
        }

        placebo_render_mode[i][screen_top_bot] = render_mode;
    }

fail:
    return reset_mode;
}

#ifdef __APPLE__
static struct mtl_ctx_t mtl_ctx[SCREEN_COUNT];
static int rashader_upscaling_update(int selected, int ctx_top_bot, int screen_top_bot) {
    int i = ctx_top_bot;

    int render_mode = -1;
    bool reset_mode = 0;
    if (selected >= 0) {
        render_mode = selected;
    } else {
        reset_mode = 1;
    }

    if (
        rashader_render[i][screen_top_bot] && (
            rashader_render_mode[i][screen_top_bot] != render_mode ||
            reset_mode
        )
    ) {
        rashader_render_close(rashader_render[i][screen_top_bot], mtl_filter_chain_free, &mtl_ctx[i]);
        rashader_render[i][screen_top_bot] = 0;
    }

    static libra_preset_ctx_t ctx = 0;
    if (!reset_mode && !rashader_render[i][screen_top_bot] && render_mode >= 0) {
        libra_error_t err = libra_preset_ctx_create(&ctx);
        if (err) {
            libra_error_print(err);
            libra_error_free(&err);
            ctx = 0;
            goto fail;
        }
        err = libra_preset_ctx_set_runtime(&ctx, LIBRA_PRESET_CTX_RUNTIME_METAL);
        if (err) {
            libra_error_print(err);
            libra_error_free(&err);
            goto fail;
        }

        mtl_ctx[i].queue = vk_demo[i].mtl_queue;
        rashader_render[i][screen_top_bot] = rashader_render_init(rashader, render_mode, &ctx, mtl_filter_chain_create, &mtl_ctx[i], mtl_filter_chain_set_param);
        if (!rashader_render[i][screen_top_bot]) {
            err_log("rashader_render_init failed\n");
            goto fail;
        }

        rashader_render_mode[i][screen_top_bot] = render_mode;
    }

fail:
    if (ctx)
        libra_preset_ctx_free(&ctx);
    return reset_mode;
}

#else

static void *vk_filter_chain_create(libra_shader_preset_t *preset, void *i) {
    struct vulkan_demo *demo = &vk_demo[(size_t)i];
    struct filter_chain_vk_opt_t opt = {
        .version = libra_instance_api_version(),
        .use_dynamic_rendering = demo->dynamic_rendering,
    };
    struct libra_device_vk_t dev = {
        .device = demo->device,
        .physical_device = demo->physical_device,
        .instance = demo->instance,
        .queue = demo->graphics_queue,
        .entry = vkGetInstanceProcAddr,
    };
    libra_vk_filter_chain_t out;
    libra_error_t err = libra_vk_filter_chain_create(preset, dev, &opt, &out);
    if (err) {
        libra_error_print(err);
        libra_error_free(&err);
        return NULL;
    }
    return out;
}

static void vk_filter_chain_free(void *fc, void *) {
    libra_error_t err = libra_vk_filter_chain_free((libra_vk_filter_chain_t *)fc);
    if (err) {
        libra_error_print(err);
        libra_error_free(&err);
    }
}

static int rashader_upscaling_update(int selected, int ctx_top_bot, int screen_top_bot) {
    int i = ctx_top_bot;

    int render_mode = -1;
    bool reset_mode = 0;
    if (selected >= 0) {
        render_mode = selected;
    } else {
        reset_mode = 1;
    }

    if (
        rashader_render[i][screen_top_bot] && (
            rashader_render_mode[i][screen_top_bot] != render_mode ||
            reset_mode
        )
    ) {
        rashader_render_close(rashader_render[i][screen_top_bot], vk_filter_chain_free, NULL);
        rashader_render[i][screen_top_bot] = 0;
    }

    static libra_preset_ctx_t ctx = 0;
    if (!reset_mode && !rashader_render[i][screen_top_bot] && render_mode >= 0) {
        libra_error_t err = libra_preset_ctx_create(&ctx);
        if (err) {
            libra_error_print(err);
            libra_error_free(&err);
            ctx = 0;
            goto fail;
        }
        err = libra_preset_ctx_set_runtime(&ctx, LIBRA_PRESET_CTX_RUNTIME_VULKAN);
        if (err) {
            libra_error_print(err);
            libra_error_free(&err);
            goto fail;
        }

        rashader_render[i][screen_top_bot] = rashader_render_init(rashader, render_mode, &ctx, vk_filter_chain_create, (void *)(intptr_t)i, (PFN_filter_chain_set_param)libra_vk_filter_chain_set_param);
        if (!rashader_render[i][screen_top_bot]) {
            err_log("rashader_render_init failed\n");
            goto fail;
        }

        rashader_render_mode[i][screen_top_bot] = render_mode;
    }

fail:
    if (ctx)
        libra_preset_ctx_free(&ctx);
    return reset_mode;
}
#endif

static void vmaAuxCleanup(void);
void ui_renderer_vk_destroy(void) {
    ui_upscaling_filters = 0;

    ui_nk_ctx = NULL;

    for (int i = 0; i < SCREEN_COUNT; ++i) {
        VkResult result;
        struct vulkan_demo *demo = &vk_demo[i];
        if (demo->render_fence) {
            result = vkWaitForFences(demo->device, 1, &demo->render_fence, VK_TRUE, UINT64_MAX);
            if (result != VK_SUCCESS) {
                err_log("vkWaitForFences failed: %d\n", result);
            }
        }

        if (demo->device) {
            result = vkDeviceWaitIdle(demo->device);
            if (result != VK_SUCCESS) {
                err_log("vkDeviceWaitIdle failed: %d\n", result);
            }
        }

        if (demo->graphics_queue) {
            result = vkQueueWaitIdle(demo->graphics_queue);
            if (result != VK_SUCCESS) {
                err_log("vkQueueWaitIdle failed: %d\n", result);
            }
        }
    }

    vk_upscaling_close();

    nk_sdl_vk_shutdown();

    vmaAuxCleanup();

    for (int i = 0; i < SCREEN_COUNT; ++i)
        vmaDestroyAllocator(vma[i]);

    for (int i = 0; i < SCREEN_COUNT; ++i)
        destroy_vulkan_demo(&vk_demo[i]);

    for (int i = 0; i < SCREEN_COUNT; ++i)
        ui_sdl_win[i] = NULL;

    sdl_win_destroy(sdl_win);

    destroy_instance(&vk_demo[SCREEN_TOP]);
}

int ui_renderer_vk_init(void) {
    VkResult result;

#ifdef STATIC_MVK
    if (!SDL_Vulkan_LoadLibrary(NULL)) {
        return -1;
    }
#else
    if (volkInitialize() != VK_SUCCESS) {
        return -1;
    }
#endif

    if (!create_instance(&vk_demo[SCREEN_TOP])) {
        return -1;
    }

#ifndef STATIC_MVK
    volkLoadInstance(vk_demo[SCREEN_TOP].instance);
#endif
    vk_demo[SCREEN_BOT].instance = vk_demo[SCREEN_TOP].instance;
    vk_demo[SCREEN_BOT].debug_messenger = vk_demo[SCREEN_TOP].debug_messenger;

    if (sdl_win_init(sdl_win, SDL_WINDOW_VULKAN)) {
        return -1;
    }

    for (int i = 0; i < SCREEN_COUNT; ++i) {
        ui_sdl_win[i] = vk_demo[i].win = sdl_win[i];
        if (!create_vulkan_demo(&vk_demo[i])) {
            err_log("failed to create vulkan resources!\n");
            return -1;
        }
    }

    sdl_set_wminfo();

    for (int i = 0; i < SCREEN_COUNT; ++i) {
        ui_win_width_drawable[i] = 1;
        ui_win_height_drawable[i] = 1;
        ui_win_scale[i] = 1.0f;
    }

    VmaVulkanFunctions vma_funcs = {};
    vma_funcs.vkAllocateMemory = vkAllocateMemory;
    vma_funcs.vkBindBufferMemory = vkBindBufferMemory;
    vma_funcs.vkBindImageMemory = vkBindImageMemory;
    vma_funcs.vkCreateBuffer = vkCreateBuffer;
    vma_funcs.vkCreateImage = vkCreateImage;
    vma_funcs.vkDestroyBuffer = vkDestroyBuffer;
    vma_funcs.vkDestroyImage = vkDestroyImage;
    vma_funcs.vkFlushMappedMemoryRanges = vkFlushMappedMemoryRanges;
    vma_funcs.vkFreeMemory = vkFreeMemory;
    vma_funcs.vkGetBufferMemoryRequirements = vkGetBufferMemoryRequirements;
    vma_funcs.vkGetImageMemoryRequirements = vkGetImageMemoryRequirements;
    vma_funcs.vkGetPhysicalDeviceMemoryProperties = vkGetPhysicalDeviceMemoryProperties;
    vma_funcs.vkGetPhysicalDeviceProperties = vkGetPhysicalDeviceProperties;
    vma_funcs.vkInvalidateMappedMemoryRanges = vkInvalidateMappedMemoryRanges;
    vma_funcs.vkMapMemory = vkMapMemory;
    vma_funcs.vkUnmapMemory = vkUnmapMemory;
    vma_funcs.vkCmdCopyBuffer = vkCmdCopyBuffer;
    vma_funcs.vkGetBufferMemoryRequirements2KHR = vkGetBufferMemoryRequirements2;
    vma_funcs.vkGetImageMemoryRequirements2KHR = vkGetImageMemoryRequirements2;
    vma_funcs.vkBindBufferMemory2KHR = vkBindBufferMemory2;
    vma_funcs.vkBindImageMemory2KHR = vkBindImageMemory2;
    vma_funcs.vkGetPhysicalDeviceMemoryProperties2KHR = vkGetPhysicalDeviceMemoryProperties2;

    VmaAllocatorCreateInfo vma_create_info = {};
    vma_create_info.flags = VMA_ALLOCATOR_CREATE_EXTERNALLY_SYNCHRONIZED_BIT;
    vma_create_info.pVulkanFunctions = &vma_funcs;
    vma_create_info.instance = vk_demo[SCREEN_TOP].instance;
    for (int i = 0; i < SCREEN_COUNT; ++i) {
        vma_create_info.vulkanApiVersion = VK_VERSION;
        vma_create_info.physicalDevice = vk_demo[i].physical_device;
        vma_create_info.device = vk_demo[i].device;
        result = vmaCreateAllocator(&vma_create_info, &vma[i]);
        if (result != VK_SUCCESS) {
            err_log("failed to create vma\n");
            return -1;
        }
    }

    struct vulkan_demo *demo_top = &vk_demo[SCREEN_TOP];
    nk_ctx = nk_sdl_vk_init(
        demo_top->win, demo_top->device, demo_top->physical_device,
        demo_top->indices.graphics, demo_top->overlay_image_views,
        demo_top->swap_chain_images_len, demo_top->swap_chain_image_format,
        0, MAX_VERTEX_BUFFER, MAX_ELEMENT_BUFFER, demo_top->graphics_queue);

    if (!nk_ctx) {
        return -1;
    }
    ui_nk_ctx = nk_ctx;

    if (vk_upscaling_init()) {
        return -1;
    }
    ui_upscaling_filters = 1;

#ifdef __APPLE__
    err_log("vulkan (via moltenvk/metal)\n");
#else
    err_log("vulkan\n");
#endif

    return 0;
}

static uint32_t vk_draw_count[SCREEN_COUNT];
void ui_renderer_vk_main(int ctx_top_bot, view_mode_t view_mode, float bg[4]) {
    int i = ctx_top_bot;
    VkResult result;
    struct vulkan_demo *demo = &vk_demo[i];

    result = vkWaitForFences(demo->device, 1, &demo->render_fence, VK_TRUE,
                                UINT64_MAX);

    if (result != VK_SUCCESS) {
        err_log("vkWaitForFences failed: %d\n", result);
        return;
    }

    result = vkResetFences(demo->device, 1, &demo->render_fence);
    if (result != VK_SUCCESS) {
        err_log("vkResetFences failed: %d\n", result);
        return;
    }

    if (
        (int)demo->swap_chain_image_extent.width != ui_win_width_drawable[i] ||
        (int)demo->swap_chain_image_extent.height != ui_win_height_drawable[i] ||
        demo->win_scale != ui_win_scale[i]
    ) {
        demo->win_width_pixel = ui_win_width_drawable[i];
        demo->win_height_pixel = ui_win_height_drawable[i];
        demo->win_scale = ui_win_scale[i];
        demo->resizing = 1;
        if (!recreate_swap_chain(demo, i == SCREEN_TOP)) {
            return;
        }
    } else if (demo->resizing) {
        demo->resizing = 0;
        if (!recreate_swap_chain(demo, i == SCREEN_TOP)) {
            return;
        }
    }

    result =
        vkAcquireNextImageKHR(demo->device, demo->swap_chain, UINT64_MAX,
                                demo->image_available, NULL, &demo->image_index);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        recreate_swap_chain(demo, i == SCREEN_TOP);

        /* If vkAcquireNextImageKHR does not successfully acquire an image,
            * semaphore and fence are unaffected. */
        return;
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        err_log("vkAcquireNextImageKHR failed: %d\n", result);
        return;
    }

    memcpy(&demo->clear_color.color, bg, sizeof(VkClearColorValue));

    vk_draw_count[i] = 0;
    if (view_mode == VIEW_MODE_TOP_BOT) {
        draw_screen(&rp_buffer_ctx[SCREEN_TOP], SCREEN_HEIGHT0, SCREEN_WIDTH, SCREEN_TOP, i, view_mode, 0);
        draw_screen(&rp_buffer_ctx[SCREEN_BOT], SCREEN_HEIGHT1, SCREEN_WIDTH, SCREEN_BOT, i, view_mode, 0);
    } else if (view_mode == VIEW_MODE_BOT) {
        draw_screen(&rp_buffer_ctx[SCREEN_BOT], SCREEN_HEIGHT1, SCREEN_WIDTH, SCREEN_BOT, i, view_mode, 0);
    } else {
        draw_screen(&rp_buffer_ctx[i], i == SCREEN_TOP ? SCREEN_HEIGHT0 : SCREEN_HEIGHT1, SCREEN_WIDTH, i, i, view_mode, 0);
    }
}

struct vk_image_t {
    VkImage img;
    VmaAllocation alloc;
    VmaAllocationInfo info;
};
struct vk_buffer_t {
    VkBuffer buf;
    VmaAllocation alloc;
    VmaAllocationInfo info;
};
struct vk_view_desc_t {
    VkImageView view;
    VkDescriptorSet desc;
};
struct vk_render_src_t {
    uint32_t width, height;
    struct vk_buffer_t staging;
    struct vk_image_t src;
    uint32_t src_mip;
    struct vk_view_desc_t src_view;
#ifdef __APPLE__
    MTLTexture_id mtl;
#endif
};

struct vk_view_fb_t {
    VkImageView view;
    VkFramebuffer fb;
};
struct vk_render_dst_t {
    uint32_t width, height;
    struct vk_image_t dst;
    struct vk_buffer_t staging;
    struct vk_view_fb_t dst_view;
};

static struct vk_render_src_t vk_render[SCREEN_COUNT][SCREEN_COUNT];

struct vk_render_img_t {
    uint32_t width, height;
    struct vk_image_t img;
    uint32_t mip;
    struct vk_view_desc_t view;
#ifdef __APPLE__
    MTLTexture_id mtl;
#endif
};

static struct vk_render_img_t vk_render_upscaled[SCREEN_COUNT][SCREEN_COUNT];

enum {
    BARRIER_SRC,
    BARRIER_DST,
    BARRIER_COUNT,
};

static struct vk_draw_t {
    VkDescriptorSet desc;
    VkViewport vp;
    VkRect2D sc;
    bool need_mips;
    uint32_t width, height;
    bool need_barrier;
    VkImageMemoryBarrier barrier;
    VkSemaphore sem, sem_in;
#ifdef __APPLE__
    uint64_t val, val_in;
#endif
    VkPipelineStageFlags stages;
} vk_draw[SCREEN_COUNT][SCREEN_COUNT];

static void vk_render_destroy(struct vulkan_demo *demo, VmaAllocator vma, struct vk_render_src_t *render);
static void vk_render_dst_destroy(struct vulkan_demo *demo, VmaAllocator vma, struct vk_render_dst_t *render);
static void vk_render_img_destroy(struct vulkan_demo *demo, VmaAllocator vma, struct vk_render_img_t *render);

static bool vk_render_create_mtl(struct vulkan_demo *demo, VmaAllocator vma, struct vk_render_src_t *render, int width, int height, __attribute__((unused)) bool mtl) {
    VkResult result;
    bool need_update_descriptor_set = false;

    if (width != (int)render->width || height != (int)render->height) {
        vk_render_destroy(demo, vma, render);
        render->width  = 0;
        render->height = 0;
    }

    VkImageSubresourceRange range = {};
    range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    range.levelCount = 1;
    range.layerCount = 1;

    VkBufferCreateInfo buf_info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    buf_info.size = width * height * GL_CHANNELS_N;
    buf_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    VmaAllocationCreateInfo alloc_info = {};
    alloc_info.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    alloc_info.usage = VMA_MEMORY_USAGE_AUTO;

    if (!render->staging.buf) {
        result = vmaCreateBuffer(vma, &buf_info, &alloc_info, &render->staging.buf, &render->staging.alloc, &render->staging.info);
        if (result != VK_SUCCESS) {
            err_log("vmaCreateBuffer staging failed: %d\n", (int)result);
            return false;
        }
    }
    if (!render->src.img) {
        VkImageCreateInfo img_info = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
#ifdef __APPLE__
        VkExportMetalObjectCreateInfoEXT mtl_img_ex_info = { VK_STRUCTURE_TYPE_EXPORT_METAL_OBJECT_CREATE_INFO_EXT };
        mtl_img_ex_info.exportObjectType = VK_EXPORT_METAL_OBJECT_TYPE_METAL_TEXTURE_BIT_EXT;
        if (mtl) {
            img_info.pNext = &mtl_img_ex_info;
        }
#endif
        img_info.imageType = VK_IMAGE_TYPE_2D;
        img_info.format = VK_FORMAT;
        img_info.extent.width = width;
        img_info.extent.height = height;
        img_info.extent.depth = 1;
        img_info.arrayLayers = 1;
        img_info.samples = VK_SAMPLE_COUNT_1_BIT;
        img_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        img_info.tiling = VK_IMAGE_TILING_OPTIMAL;
#ifdef __APPLE__
        img_info.mipLevels = 1;
#else
        img_info.mipLevels = floorf(log2f(MAX(img_info.extent.width, img_info.extent.height))) + 1;
#endif
        img_info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        img_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        alloc_info.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
        result = vmaCreateImage(vma, &img_info, &alloc_info, &render->src.img, &render->src.alloc, &render->src.info);
        if (result != VK_SUCCESS) {
            err_log("vmaCreateImage src failed: %d\n", (int)result);
            return false;
        }
        render->src_mip = img_info.mipLevels;
#ifdef __APPLE__
        if (mtl) {
            VkExportMetalObjectsInfoEXT mtl_ex_info = { VK_STRUCTURE_TYPE_EXPORT_METAL_OBJECTS_INFO_EXT };
            VkExportMetalTextureInfoEXT mtl_ex_tex_info = { VK_STRUCTURE_TYPE_EXPORT_METAL_TEXTURE_INFO_EXT };
            mtl_ex_tex_info.image = render->src.img;
            mtl_ex_tex_info.plane = VK_IMAGE_ASPECT_PLANE_0_BIT;
            mtl_ex_info.pNext = &mtl_ex_tex_info;
            vkExportMetalObjectsEXT(demo->device, &mtl_ex_info);
            render->mtl = mtl_ex_tex_info.mtlTexture;
        }
#endif
    }

    VkImageSubresourceRange range_mip = range;
    range_mip.levelCount = render->src_mip;

    if (!render->src_view.view) {
        VkImageViewCreateInfo view_info = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.image = render->src.img;
        view_info.format = VK_FORMAT;
        view_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_info.subresourceRange = range_mip;
        result = vkCreateImageView(demo->device, &view_info, NULL, &render->src_view.view);
        if (result != VK_SUCCESS) {
            err_log("vkCreateImageView src_view failed: %d\n", (int)result);
            return false;
        }
        need_update_descriptor_set = true;
    }
    if (!render->src_view.desc) {
        VkDescriptorSetAllocateInfo alloc_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        alloc_info.descriptorPool = demo->descriptor_pool;
        alloc_info.descriptorSetCount = 1;
        alloc_info.pSetLayouts = &demo->descriptor_set_layout;
        result = vkAllocateDescriptorSets(demo->device, &alloc_info,
            &render->src_view.desc);
        if (result != VK_SUCCESS) {
            err_log("vkAllocateDescriptorSets src_view failed: %d\n", result);
            return false;
        }
        need_update_descriptor_set = true;
    }

    if (need_update_descriptor_set) {
        VkDescriptorImageInfo descriptor_image_info;
        VkWriteDescriptorSet descriptor_write;
        descriptor_image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        descriptor_image_info.sampler = demo->sampler;
        descriptor_image_info.imageView = render->src_view.view;
        descriptor_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptor_write.dstBinding = 0;
        descriptor_write.dstArrayElement = 0;
        descriptor_write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptor_write.descriptorCount = 1;
        descriptor_write.pImageInfo = &descriptor_image_info;
        descriptor_write.dstSet = render->src_view.desc;
        vkUpdateDescriptorSets(demo->device, 1, &descriptor_write, 0, NULL);
    }

    render->width  = width;
    render->height = height;

    return true;
}

static bool vk_render_create(struct vulkan_demo *demo, VmaAllocator vma, struct vk_render_src_t *render, int width, int height) {
    return vk_render_create_mtl(demo, vma, render, width, height, 0);
}
static bool vk_render_dst_create(struct vulkan_demo *demo, VmaAllocator vma, struct vk_render_dst_t *render, VkRenderPass render_pass, int width, int height) {
    VkResult result;

    if (width != (int)render->width || height != (int)render->height) {
        vk_render_dst_destroy(demo, vma, render);
        render->width  = 0;
        render->height = 0;
    }

    VkBufferCreateInfo buf_info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    buf_info.size = width * height * GL_CHANNELS_N;
    buf_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    VkImageSubresourceRange range = {};
    range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    range.levelCount = 1;
    range.layerCount = 1;

    VmaAllocationCreateInfo alloc_info = {};
    alloc_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
    alloc_info.usage = VMA_MEMORY_USAGE_AUTO;

    if (!render->staging.buf) {
        result = vmaCreateBuffer(vma, &buf_info, &alloc_info, &render->staging.buf, &render->staging.alloc, &render->staging.info);
        if (result != VK_SUCCESS) {
            err_log("vmaCreateImage staging failed: %d\n", (int)result);
            return false;
        }
    }
    if (!render->dst.img) {
        VkImageCreateInfo img_info = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        img_info.imageType = VK_IMAGE_TYPE_2D;
        img_info.format = VK_FORMAT;
        img_info.extent.width = width;
        img_info.extent.height = height;
        img_info.extent.depth = 1;
        img_info.mipLevels = 1;
        img_info.arrayLayers = 1;
        img_info.samples = VK_SAMPLE_COUNT_1_BIT;
        img_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        img_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        img_info.tiling = VK_IMAGE_TILING_OPTIMAL;
        img_info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        alloc_info.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
        result = vmaCreateImage(vma, &img_info, &alloc_info, &render->dst.img, &render->dst.alloc, &render->dst.info);
        if (result != VK_SUCCESS) {
            err_log("vmaCreateImage dst failed: %d\n", (int)result);
            return false;
        }
    }

    if (!render->dst_view.view) {
        VkImageViewCreateInfo view_info = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.image = render->dst.img;
        view_info.format = VK_FORMAT;
        view_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_info.subresourceRange = range;
        result = vkCreateImageView(demo->device, &view_info, NULL, &render->dst_view.view);
        if (result != VK_SUCCESS) {
            err_log("vkCreateImageView dst_view failed: %d\n", (int)result);
            return false;
        }
    }
    if (!render->dst_view.fb) {
        VkFramebufferCreateInfo fb_info = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
        fb_info.renderPass = render_pass;
        fb_info.attachmentCount = 1;
        fb_info.width = width;
        fb_info.height = height;
        fb_info.layers = 1;
        fb_info.pAttachments = &render->dst_view.view;

        result = vkCreateFramebuffer(demo->device, &fb_info, NULL,
                                     &render->dst_view.fb);
        if (result != VK_SUCCESS) {
            err_log("vkCreateFramebuffer dst_view failed: %d\n", result);
            return false;
        }
    }

    render->width  = width;
    render->height = height;

    return true;
}

static bool vk_render_img_create(struct vulkan_demo *demo, VmaAllocator vma, struct vk_render_img_t *render, int width, int height) {
    VkResult result;
    bool need_update_descriptor_set = false;

    if (width != (int)render->width || height != (int)render->height) {
        vk_render_img_destroy(demo, vma, render);
        render->width  = 0;
        render->height = 0;
    }

    VkImageSubresourceRange range = {};
    range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    range.levelCount = 1;
    range.layerCount = 1;

    if (!render->img.img) {
        VkImageCreateInfo img_info = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
#ifdef __APPLE__
        VkExportMetalObjectCreateInfoEXT mtl_img_ex_info = { VK_STRUCTURE_TYPE_EXPORT_METAL_OBJECT_CREATE_INFO_EXT };
        mtl_img_ex_info.exportObjectType = VK_EXPORT_METAL_OBJECT_TYPE_METAL_TEXTURE_BIT_EXT;
        img_info.pNext = &mtl_img_ex_info;
#endif
        img_info.imageType = VK_IMAGE_TYPE_2D;
        img_info.format = VK_FORMAT;
        img_info.extent.width = width;
        img_info.extent.height = height;
        img_info.extent.depth = 1;
#ifdef __APPLE__
        img_info.mipLevels = 1;
#else
        img_info.mipLevels = floorf(log2f(MAX(img_info.extent.width, img_info.extent.height))) + 1;
#endif
        img_info.arrayLayers = 1;
        img_info.samples = VK_SAMPLE_COUNT_1_BIT;
        img_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        img_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        img_info.tiling = VK_IMAGE_TILING_OPTIMAL;
        img_info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        VmaAllocationCreateInfo alloc_info = {};
        alloc_info.usage = VMA_MEMORY_USAGE_AUTO;
        alloc_info.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
        result = vmaCreateImage(vma, &img_info, &alloc_info, &render->img.img, &render->img.alloc, &render->img.info);
        if (result != VK_SUCCESS) {
            err_log("vmaCreateImage dst failed: %d\n", (int)result);
            return false;
        }
        render->mip = img_info.mipLevels;
#ifdef __APPLE__
        VkExportMetalObjectsInfoEXT mtl_ex_info = { VK_STRUCTURE_TYPE_EXPORT_METAL_OBJECTS_INFO_EXT };
        VkExportMetalTextureInfoEXT mtl_ex_tex_info = { VK_STRUCTURE_TYPE_EXPORT_METAL_TEXTURE_INFO_EXT };
        mtl_ex_tex_info.image = render->img.img;
        mtl_ex_tex_info.plane = VK_IMAGE_ASPECT_PLANE_0_BIT;
        mtl_ex_info.pNext = &mtl_ex_tex_info;
        vkExportMetalObjectsEXT(demo->device, &mtl_ex_info);
        render->mtl = mtl_ex_tex_info.mtlTexture;
#endif
    }

    VkImageSubresourceRange range_mip = range;
    range_mip.levelCount = render->mip;

    if (!render->view.view) {
        VkImageViewCreateInfo view_info = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.image = render->img.img;
        view_info.format = VK_FORMAT;
        view_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_info.subresourceRange = range_mip;
        result = vkCreateImageView(demo->device, &view_info, NULL, &render->view.view);
        if (result != VK_SUCCESS) {
            err_log("vkCreateImageView dst_view failed: %d\n", (int)result);
            return false;
        }
        need_update_descriptor_set = true;
    }
    if (!render->view.desc) {
        VkDescriptorSetAllocateInfo alloc_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        alloc_info.descriptorPool = demo->descriptor_pool;
        alloc_info.descriptorSetCount = 1;
        alloc_info.pSetLayouts = &demo->descriptor_set_layout;
        result = vkAllocateDescriptorSets(demo->device, &alloc_info,
            &render->view.desc);
        if (result != VK_SUCCESS) {
            err_log("vkAllocateDescriptorSets src_view failed: %d\n", result);
            return false;
        }
        need_update_descriptor_set = true;
    }

    if (need_update_descriptor_set) {
        VkDescriptorImageInfo descriptor_image_info;
        VkWriteDescriptorSet descriptor_write;
        descriptor_image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        descriptor_image_info.sampler = demo->sampler;
        descriptor_image_info.imageView = render->view.view;
        descriptor_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptor_write.dstBinding = 0;
        descriptor_write.dstArrayElement = 0;
        descriptor_write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptor_write.descriptorCount = 1;
        descriptor_write.pImageInfo = &descriptor_image_info;
        descriptor_write.dstSet = render->view.desc;
        vkUpdateDescriptorSets(demo->device, 1, &descriptor_write, 0, NULL);
    }

    render->width  = width;
    render->height = height;

    return true;
}

static void vk_gen_mip_maps(VkCommandBuffer cmd, VkImage img, uint32_t mip, int width, int height, bool layout) {
    VkImageSubresourceRange range = {};
    range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    range.levelCount = 1;
    range.layerCount = 1;

    VkImageMemoryBarrier barrier[BARRIER_COUNT] = {};
    barrier[BARRIER_SRC].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier[BARRIER_SRC].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier[BARRIER_SRC].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier[BARRIER_SRC].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier[BARRIER_SRC].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier[BARRIER_SRC].image = img;
    barrier[BARRIER_SRC].subresourceRange = range;
    barrier[BARRIER_SRC].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier[BARRIER_SRC].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier[BARRIER_DST].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier[BARRIER_DST].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier[BARRIER_DST].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier[BARRIER_DST].image = img;
    barrier[BARRIER_DST].subresourceRange = range;
    barrier[BARRIER_DST].srcAccessMask = 0;
    barrier[BARRIER_DST].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier[BARRIER_DST].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier[BARRIER_DST].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

    for (uint32_t i = 1; i < mip; ++i) {
        VkImageBlit blt = {};
        blt.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blt.srcSubresource.layerCount = 1;
        blt.srcSubresource.mipLevel = i - 1;
        blt.srcOffsets[1].x = MAX((uint32_t)width >> (i - 1), 1);
        blt.srcOffsets[1].y = MAX((uint32_t)height >> (i - 1), 1);
        blt.srcOffsets[1].z = 1;
        blt.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blt.dstSubresource.layerCount = 1;
        blt.dstSubresource.mipLevel = i;
        blt.dstOffsets[1].x = MAX((uint32_t)width >> i, 1);
        blt.dstOffsets[1].y = MAX((uint32_t)height >> i, 1);
        blt.dstOffsets[1].z = 1;

        barrier[BARRIER_SRC].subresourceRange.baseMipLevel = i - 1;
        barrier[BARRIER_DST].subresourceRange.baseMipLevel = i;
        if (i == 1 && layout) {
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &barrier[BARRIER_DST]);
        } else {
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, BARRIER_COUNT, barrier);
        }
        vkCmdBlitImage(cmd, img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blt, VK_FILTER_LINEAR);
    }

    barrier[BARRIER_SRC].subresourceRange.baseMipLevel = mip - 1;
    if (mip == 1 && layout) {
    } else {
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &barrier[BARRIER_SRC]);
    }
}

static void vk_render_upload_and_gen_mip_maps(VkCommandBuffer cmd, VmaAllocator vma, struct vk_render_src_t *render, const void *data, int width, int height) {
    VkResult result;

    VkImageSubresourceRange range = {};
    range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    range.levelCount = 1;
    range.layerCount = 1;

    result = vmaCopyMemoryToAllocation(vma, data, render->staging.alloc, 0, width * height * GL_CHANNELS_N);
    if (result != VK_SUCCESS) {
        err_log("vmaCopyMemoryToAllocation staging failed: %d\n", (int)result);
        return;
    }

    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.image = render->src.img;
    barrier.subresourceRange = range;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);

    VkImageSubresourceLayers layer = {};
    layer.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    layer.layerCount = 1;
    VkOffset3D offset = { 0, 0, 0 };
    VkExtent3D extent = { width, height, 1 };
    VkBufferImageCopy region = {};
    region.imageSubresource = layer;
    region.imageOffset = offset;
    region.imageExtent = extent;

    vkCmdCopyBufferToImage(cmd, render->staging.buf, render->src.img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    vk_gen_mip_maps(cmd, render->src.img, render->src_mip, width, height, 0);
}

enum upscaling_t {
    UPSCALING_NONE,
    UPSCALING_PLACEBO,
    UPSCALING_RASHADER,
};

static struct ui_prev_dims_t {
    int upscaling_selected, width, height;
} ui_prev_dims[SCREEN_COUNT][SCREEN_COUNT];

void ui_renderer_vk_draw(uint8_t *data, int width, int height, int screen_top_bot, int ctx_top_bot, view_mode_t view_mode) {
    int i = ctx_top_bot;
    struct vulkan_demo *demo = &vk_demo[i];
    struct vk_render_src_t *render = &vk_render[i][screen_top_bot];

#ifdef __APPLE__
    if (!vk_render_create_mtl(demo, vma[i], render, height, width, 1))
        return;
#else
    if (!vk_render_create(demo, vma[i], render, height, width))
        return;
#endif

    int ctx_left;
    int ctx_top;
    int ctx_width;
    int ctx_height;
    draw_screen_get_dims_lite(screen_top_bot, i, view_mode, width, height, &ctx_left, &ctx_top, &ctx_width, &ctx_height);
    ctx_left *= ui_win_scale[i];
    ctx_top *= ui_win_scale[i];
    ctx_width *= ui_win_scale[i];
    ctx_height *= ui_win_scale[i];

    struct vk_draw_t *draw = &vk_draw[i][vk_draw_count[i]];
    draw->sc.offset.x = draw->vp.x = ctx_left;
    draw->sc.offset.y = draw->vp.y = ctx_top;
    draw->sc.extent.width = draw->vp.width = MAX(ctx_width, 1);
    draw->sc.extent.height = draw->vp.height = MAX(ctx_height, 1);
    draw->vp.minDepth = 0;
    draw->vp.maxDepth = 1;

    VkImageSubresourceRange range = {};
    range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    range.levelCount = 1;
    range.layerCount = 1;

    VkImageSubresourceRange range_mip = range;
    range_mip.levelCount = render->src_mip;

    int upscaling_selected = ui_upscaling_selected;
    enum upscaling_t upscaling = UPSCALING_NONE;
    draw->desc = render->src_view.desc;
    draw->need_mips = 0;
    draw->sem_in = 0;

    struct ui_prev_dims_t *prev = &ui_prev_dims[i][screen_top_bot];
    bool need_tex_update = data || prev->upscaling_selected != upscaling_selected || prev->width != ctx_width || prev->height != ctx_height;

    if (data) {
        VkCommandBufferBeginInfo command_buffer_begin_info;
        memset(&command_buffer_begin_info, 0, sizeof(VkCommandBufferBeginInfo));
        command_buffer_begin_info.sType =
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        command_buffer_begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        VkCommandBuffer command_buffer = demo->upload_command_buffers[screen_top_bot][demo->image_index];
        VkResult result;
        result = vkBeginCommandBuffer(command_buffer, &command_buffer_begin_info);

        if (result != VK_SUCCESS) {
            err_log("vkBeginCommandBuffer failed: %d\n", result);
            return;
        }

        vk_render_upload_and_gen_mip_maps(command_buffer, vma[i], render, data, height, width);

        result = vkEndCommandBuffer(command_buffer);
        if (result != VK_SUCCESS) {
            err_log("vkEndCommandBuffer failed: %d\n", result);
            return;
        }

        VkSubmitInfo submit_info;
        memset(&submit_info, 0, sizeof(VkSubmitInfo));
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &command_buffer;
        submit_info.signalSemaphoreCount = 1;
        submit_info.pSignalSemaphores = &demo->upload_sem[screen_top_bot];
#ifdef __APPLE__
        VkTimelineSemaphoreSubmitInfo tl_info = { VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO };
        tl_info.signalSemaphoreValueCount = 1;
        uint64_t signal_val = demo->upload_val[screen_top_bot];
        tl_info.pSignalSemaphoreValues = &signal_val;
        submit_info.pNext = &tl_info;
#endif

        result = vkQueueSubmit(demo->graphics_queue, 1, &submit_info, 0);

        if (result != VK_SUCCESS) {
            err_log("vkQueueSubmit failed: %d\n", result);
            return;
        }
    }

    struct vk_render_img_t *render_upscaled = &vk_render_upscaled[i][screen_top_bot];
    if (need_tex_update) {
        if (!vk_render_img_create(demo, vma[i], render_upscaled, ctx_height, ctx_width)) {
            goto upscale_fail;
        }

        if (!IS_PLACEBO(upscaling_selected)) {
            placebo_upscaling_update(-1, i, screen_top_bot);
        }

        if (!IS_RASHADER(upscaling_selected)) {
            rashader_upscaling_update(-1, i, screen_top_bot);
        }

        pl_tex in_tex = NULL;
        pl_tex out_tex = NULL;
        if (IS_PLACEBO(upscaling_selected)) {
            int reset_mode = placebo_upscaling_update(PLACEBO_MODE(upscaling_selected), i, screen_top_bot);
            if (placebo_render[i][screen_top_bot]) {
                struct pl_vulkan_wrap_params in_tex_pars = {};
                in_tex_pars.image = render->src.img;
                in_tex_pars.width = height;
                in_tex_pars.height = width;
                in_tex_pars.format = VK_FORMAT;
                in_tex_pars.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
                in_tex = pl_vulkan_wrap(pl_vk_dev[i]->gpu, &in_tex_pars);
                if (!in_tex) {
                    goto placebo_fail;
                }

                struct pl_vulkan_wrap_params out_tex_pars = {};
                out_tex_pars.image = render_upscaled->img.img;
                out_tex_pars.width = ctx_height;
                out_tex_pars.height = ctx_width;
                out_tex_pars.format = VK_FORMAT;
                out_tex_pars.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
                out_tex = pl_vulkan_wrap(pl_vk_dev[i]->gpu, &out_tex_pars);
                if (!out_tex) {
                    goto placebo_fail;
                }

                pl_vulkan_release_ex(pl_vk_dev[i]->gpu, pl_vulkan_release_params(
                    .tex = in_tex,
                    .layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    .qf = demo->indices.graphics,
                    .semaphore = { data ? demo->upload_sem[screen_top_bot] : 0,
#ifdef __APPLE__
                        data ? demo->upload_val[screen_top_bot]++ : 0,
#endif
                    },
                ));

                pl_vulkan_release_ex(pl_vk_dev[i]->gpu, pl_vulkan_release_params(
                    .tex = out_tex,
                    .layout = VK_IMAGE_LAYOUT_UNDEFINED,
                    .qf = demo->indices.graphics,
                ));

                bool ret = placebo_render_run(placebo_render[i][screen_top_bot], in_tex, out_tex, 0, 0) != NULL;
                if (!ret) {
                    goto placebo_fail;
                }

                if (!pl_vulkan_hold_ex(pl_vk_dev[i]->gpu, pl_vulkan_hold_params(
                    .tex = in_tex,
                    .layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    .qf = demo->indices.graphics,
                    .semaphore = { placebo_in_sem[i][screen_top_bot],
#ifdef __APPLE__
                        placebo_in_val[i][screen_top_bot],
#endif
                    },
                ))) {
                    goto placebo_fail;
                }

                if (!pl_vulkan_hold_ex(pl_vk_dev[i]->gpu, pl_vulkan_hold_params(
                    .tex = out_tex,
                    .layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    .qf = demo->indices.graphics,
                    .semaphore = { placebo_sem[i][screen_top_bot],
#ifdef __APPLE__
                        placebo_val[i][screen_top_bot],
#endif
                    },
                ))) {
                    goto placebo_fail;
                }

                upscaling = UPSCALING_PLACEBO;
            } else if (!reset_mode) {
placebo_fail:
                err_log("placebo render failed\n");
                ui_upscaling_selected = UPSCALING_DEFAULT_NONE;
            }
        }

        if (in_tex)
            pl_tex_destroy(pl_vk_dev[i]->gpu, &in_tex);
        if (out_tex)
            pl_tex_destroy(pl_vk_dev[i]->gpu, &out_tex);

        if (IS_RASHADER(upscaling_selected)) {
            int reset_mode = rashader_upscaling_update(RASHADER_MODE(upscaling_selected), i, screen_top_bot);
            if (rashader_render[i][screen_top_bot]) {
#ifdef __APPLE__
                bool fail = 1;
                if (mtl_filter_chain_frame(
                    rashader_render[i][screen_top_bot],
                    &mtl_ctx[i],
                    demo->upload_evt[screen_top_bot], demo->libra_evt[screen_top_bot],
                    demo->upload_val[screen_top_bot]++, demo->libra_val[screen_top_bot],
                    render->mtl, render_upscaled->mtl)
                ) {
                    fail = 0;
                }
#else
                libra_vk_filter_chain_t *chain = rashader_render_chain(rashader_render[i][screen_top_bot]);
                struct libra_image_vk_t image = {
                    .handle = render->src.img,
                    .format = VK_FORMAT,
                    .width = height,
                    .height = width,
                };

                struct libra_image_vk_t out = {
                    .handle = render_upscaled->img.img,
                    .format = VK_FORMAT,
                    .width = ctx_height,
                    .height = ctx_width,
                };

                VkCommandBufferBeginInfo command_buffer_begin_info;
                memset(&command_buffer_begin_info, 0, sizeof(VkCommandBufferBeginInfo));
                command_buffer_begin_info.sType =
                    VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
                command_buffer_begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

                VkCommandBuffer command_buffer = demo->libra_command_buffers[screen_top_bot][demo->image_index];
                VkResult result;
                result = vkBeginCommandBuffer(command_buffer, &command_buffer_begin_info);

                if (result != VK_SUCCESS) {
                    err_log("vkBeginCommandBuffer failed: %d\n", result);
                    return;
                }

                VkImageMemoryBarrier barrier[BARRIER_COUNT] = {};
                barrier[BARRIER_SRC].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                barrier[BARRIER_SRC].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                barrier[BARRIER_SRC].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                barrier[BARRIER_SRC].image = render->src.img;
                barrier[BARRIER_SRC].subresourceRange = range_mip;
                barrier[BARRIER_SRC].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                barrier[BARRIER_SRC].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                barrier[BARRIER_SRC].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier[BARRIER_SRC].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier[BARRIER_DST].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                barrier[BARRIER_DST].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                barrier[BARRIER_DST].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                barrier[BARRIER_DST].image = render_upscaled->img.img;
                barrier[BARRIER_DST].subresourceRange = range;
                barrier[BARRIER_DST].srcAccessMask = 0;
                barrier[BARRIER_DST].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                barrier[BARRIER_DST].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier[BARRIER_DST].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, NULL, 0, NULL, BARRIER_COUNT,
                    barrier);

                bool fail = 0;
                libra_error_t err = libra_vk_filter_chain_frame(chain, command_buffer, 1, image, out, NULL, NULL, NULL);
                if (err) {
                    libra_error_print(err);
                    libra_error_free(&err);
                    fail = 1;
                }

                barrier[BARRIER_SRC].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                barrier[BARRIER_SRC].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                barrier[BARRIER_SRC].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
                barrier[BARRIER_SRC].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                barrier[BARRIER_DST].oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                barrier[BARRIER_DST].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                barrier[BARRIER_DST].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                barrier[BARRIER_DST].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                    VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, BARRIER_COUNT,
                    barrier);


                result = vkEndCommandBuffer(command_buffer);
                if (result != VK_SUCCESS) {
                    err_log("vkEndCommandBuffer failed: %d\n", result);
                    return;
                }

                VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
                VkSubmitInfo submit_info;
                memset(&submit_info, 0, sizeof(VkSubmitInfo));
                submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
                submit_info.commandBufferCount = 1;
                submit_info.pCommandBuffers = &command_buffer;
                submit_info.waitSemaphoreCount = data ? 1 : 0;
                submit_info.pWaitSemaphores = data ? &demo->upload_sem[screen_top_bot] : 0;
                submit_info.pWaitDstStageMask = data ? &wait_stage : 0;
                submit_info.signalSemaphoreCount = 1;
                submit_info.pSignalSemaphores = &demo->libra_sem[screen_top_bot];

                result = vkQueueSubmit(demo->graphics_queue, 1, &submit_info, 0);

                if (result != VK_SUCCESS) {
                    err_log("vkQueueSubmit failed: %d\n", result);
                    return;
                }
#endif
                if (fail)
                    goto rashader_fail;

                upscaling = UPSCALING_RASHADER;
            } else if (!reset_mode) {
rashader_fail:
                err_log("rashader render failed\n");
                ui_upscaling_selected = UPSCALING_DEFAULT_NONE;
            }
        }

upscale_fail:
        if (ui_upscaling_selected == UPSCALING_DEFAULT_NONE) {
            upscaling = UPSCALING_NONE;
        }
    }

    if (need_tex_update) {
        switch(upscaling) {
            default:
            case UPSCALING_NONE:
                draw->barrier = (VkImageMemoryBarrier){ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
                draw->barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                draw->barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                draw->barrier.image = render->src.img;
                draw->barrier.subresourceRange = range_mip;
                draw->barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                draw->barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                draw->barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                draw->barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                draw->need_barrier = 1;
                draw->sem = demo->upload_sem[screen_top_bot];
#ifdef __APPLE__
                draw->val = demo->upload_val[screen_top_bot]++;
#endif
                draw->stages = VK_PIPELINE_STAGE_TRANSFER_BIT;
                break;

            case UPSCALING_PLACEBO:
                draw->barrier = (VkImageMemoryBarrier){ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
                draw->barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                draw->barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                draw->barrier.image = render_upscaled->img.img;
                draw->barrier.subresourceRange = range;
                draw->barrier.subresourceRange.levelCount = render_upscaled->mip;
                draw->barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                draw->barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                draw->barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                draw->barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                draw->need_barrier = 1;
                draw->width = render_upscaled->width;
                draw->height = render_upscaled->height;
                draw->need_mips = 1;
                draw->sem = placebo_sem[i][screen_top_bot];
                draw->sem_in = placebo_in_sem[i][screen_top_bot];
#ifdef __APPLE__
                draw->val = placebo_val[i][screen_top_bot]++;
                draw->val_in = placebo_in_val[i][screen_top_bot]++;
#endif
                draw->stages = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
                draw->desc = render_upscaled->view.desc;
                break;

            case UPSCALING_RASHADER:
                draw->barrier = (VkImageMemoryBarrier){ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
                draw->barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                draw->barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                draw->barrier.image = render_upscaled->img.img;
                draw->barrier.subresourceRange = range;
                draw->barrier.subresourceRange.levelCount = render_upscaled->mip;
                draw->barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                draw->barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                draw->barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                draw->barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                draw->need_barrier = 1;
                draw->width = render_upscaled->width;
                draw->height = render_upscaled->height;
                draw->need_mips = 1;
                draw->sem = demo->libra_sem[screen_top_bot];
#ifdef __APPLE__
                draw->val = demo->libra_val[screen_top_bot]++;
#endif
                draw->stages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
                draw->desc = render_upscaled->view.desc;
                break;
        }
    } else {
        draw->need_barrier = 0;
        draw->sem = 0;
        if (IS_PLACEBO(prev->upscaling_selected) || IS_RASHADER(prev->upscaling_selected)) {
            draw->desc = render_upscaled->view.desc;
        }
    }
    ++vk_draw_count[i];

    prev->upscaling_selected = upscaling_selected;
    prev->width = ctx_width;
    prev->height = ctx_height;
}

void ui_renderer_vk_present(int ctx_top_bot) {
    int i = ctx_top_bot;
    VkResult result;
    VkCommandBufferBeginInfo command_buffer_begin_info;
    VkCommandBuffer command_buffer;
    VkSubmitInfo submit_info;
    VkRenderPassBeginInfo render_pass_info;
    VkPresentInfoKHR present_info;
    VkPipelineStageFlags wait_stage =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSemaphore nk_semaphore = NULL;
// see struct vk_draw_t for this number
#define SEM_COUNT_MAX (SCREEN_COUNT * 2 + 1)
    VkSemaphore sems[SEM_COUNT_MAX];
    VkPipelineStageFlags stages[SEM_COUNT_MAX];
#ifdef __APPLE__
    uint64_t vals[SEM_COUNT_MAX];
#endif
    uint32_t sems_count = 0;
    struct vulkan_demo *demo = &vk_demo[i];
    bool ret;

    if (i == SCREEN_TOP) {
        if (nk_gui_next) {
            nk_semaphore = nk_sdl_vk_render(
                demo->image_index,
                demo->image_available, NK_ANTI_ALIASING_OFF);
            nk_gui_next = 0;
        }
    }

    memset(&command_buffer_begin_info, 0, sizeof(VkCommandBufferBeginInfo));
    command_buffer_begin_info.sType =
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    command_buffer_begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    command_buffer = demo->command_buffers[demo->image_index];
    result = vkBeginCommandBuffer(command_buffer, &command_buffer_begin_info);

    if (result != VK_SUCCESS) {
        err_log("vkBeginCommandBuffer failed: %d\n", result);
        return;
    }

    for (uint32_t k = 0; k < vk_draw_count[i]; ++k) {
        struct vk_draw_t *draw = &vk_draw[i][k];
        if (draw->need_mips) {
            vk_gen_mip_maps(command_buffer, draw->barrier.image, draw->barrier.subresourceRange.levelCount,
                draw->width, draw->height, 1);
        }
        if (draw->need_barrier) {
            vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL, 0, NULL, 1,
                &draw->barrier);
        }
        if (draw->sem) {
            sems[sems_count] = draw->sem;
            stages[sems_count] = draw->stages;
#ifdef __APPLE__
            vals[sems_count] = draw->val;
#endif
            ++sems_count;
        }
        if (draw->sem_in) {
            sems[sems_count] = draw->sem_in;
#ifdef __APPLE__
            vals[sems_count] = draw->val_in;
#endif
            stages[sems_count] = draw->stages;
            ++sems_count;
        }
    }

    memset(&render_pass_info, 0, sizeof(VkRenderPassBeginInfo));
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    render_pass_info.renderPass = demo->render_pass;
    render_pass_info.framebuffer = demo->framebuffers[demo->image_index];
    render_pass_info.renderArea.offset.x = 0;
    render_pass_info.renderArea.offset.y = 0;
    render_pass_info.renderArea.extent = demo->swap_chain_image_extent;
    render_pass_info.clearValueCount = 1;
    render_pass_info.pClearValues = &demo->clear_color;

    vkCmdBeginRenderPass(command_buffer, &render_pass_info,
                         VK_SUBPASS_CONTENTS_INLINE);

    for (uint32_t k = 0; k < vk_draw_count[i]; ++k) {
        struct vk_draw_t *draw = &vk_draw[i][k];
        vkCmdBindPipeline(
            command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
            demo->data_pipeline);
        vkCmdBindDescriptorSets(
            command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
            demo->pipeline_layout, 0, 1,
            &draw->desc, 0, NULL);
        vkCmdSetScissor(command_buffer, 0, 1, &draw->sc);
        vkCmdSetViewport(command_buffer, 0, 1, &draw->vp);
        vkCmdDraw(command_buffer, 3, 1, 0, 0);
    }
    if (nk_semaphore) {
        vkCmdBindPipeline(
            command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
            demo->pipeline);
        vkCmdBindDescriptorSets(
            command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
            demo->pipeline_layout, 0, 1,
            &demo->descriptor_sets[demo->image_index], 0, NULL);

        VkViewport viewport;
        VkRect2D scissor;
        memset(&viewport, 0, sizeof(VkViewport));
        memset(&scissor, 0, sizeof(VkRect2D));
        scissor.offset.x = viewport.x = 0.0f;
        scissor.offset.y = viewport.y = 0.0f;
        scissor.extent.width = viewport.width = (float)demo->swap_chain_image_extent.width;
        scissor.extent.height = viewport.height = (float)demo->swap_chain_image_extent.height;
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetScissor(command_buffer, 0, 1, &scissor);
        vkCmdSetViewport(command_buffer, 0, 1, &viewport);

        vkCmdDraw(command_buffer, 3, 1, 0, 0);
    } else {
        nk_semaphore = demo->image_available;
        wait_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    }

    sems[sems_count] = nk_semaphore;
    stages[sems_count] = wait_stage;
    vals[sems_count] = 0;
    ++sems_count;

    vkCmdEndRenderPass(command_buffer);

    result = vkEndCommandBuffer(command_buffer);
    if (result != VK_SUCCESS) {
        err_log("vkEndCommandBuffer failed: %d\n", result);
        return;
    }

    memset(&submit_info, 0, sizeof(VkSubmitInfo));
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.waitSemaphoreCount = sems_count;
    submit_info.pWaitSemaphores = sems;
    submit_info.pWaitDstStageMask = stages;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_buffer;
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores = &demo->render_finished;

#ifdef __APPLE__
    VkTimelineSemaphoreSubmitInfo tl_info = { VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO };
    tl_info.waitSemaphoreValueCount = sems_count;
    tl_info.pWaitSemaphoreValues = vals;
    tl_info.signalSemaphoreValueCount = 1;
    uint64_t signal_value = 0;
    tl_info.pSignalSemaphoreValues = &signal_value;
    submit_info.pNext = &tl_info;
#endif

    result = vkQueueSubmit(demo->graphics_queue, 1, &submit_info,
                           demo->render_fence);

    if (result != VK_SUCCESS) {
        err_log("vkQueueSubmit failed: %d\n", result);
        return;
    }

    memset(&present_info, 0, sizeof(VkPresentInfoKHR));
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores = &demo->render_finished;
    present_info.swapchainCount = 1;
    present_info.pSwapchains = &demo->swap_chain;
    present_info.pImageIndices = &demo->image_index;

    result = vkQueuePresentKHR(demo->present_queue, &present_info);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        ret = recreate_swap_chain(demo, i == SCREEN_TOP);

        if (!ret) {
            err_log("failed to recreate swap chain!\n");
        }
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        err_log("vkQueuePresentKHR failed: %d\n", result);
    }
}

static struct vk_render_src_t cursor_src;
static struct vk_render_dst_t cursor_dst;

void ui_renderer_vk_gen_cursor(stbi_t *image, const unsigned char *base, int width, int height, int channels, float scale) {
    if (channels != GL_CHANNELS_N) {
        return;
    }
    bool fail = true;

    int i = SCREEN_TOP;

    int target_width = roundf(width * scale);
    int target_height = roundf(height * scale);

    struct vulkan_demo *demo = &vk_demo[i];

    image->image = malloc(target_width * target_height * channels);
    if (!image->image) {
        return;
    }

    unsigned char *base2 = malloc(width * height * GL_CHANNELS_N);
    if (!base2) {
        goto fail_base2;
    }

    unsigned char *image_base2 = malloc(target_width * target_height * GL_CHANNELS_N);
    if (!image_base2) {
        goto fail_image_base2;
    }

    for (int x = 0; x < width; ++x) {
        for (int y = 0; y < height; ++y) {
            int base2_i = (y * width + x) * GL_CHANNELS_N;
            int base_i = (y * width + x) * channels;
            base2[base2_i + 2] = base2[base2_i] = ((int)base[base_i] + (int)base[base_i + 1] + (int)base[base_i + 2]) / 3;
            base2[base2_i + 1] = base[base_i + 3];
            base2[base2_i + 3] = UCHAR_MAX;
        }
    }

    VkResult result;
    result = vkWaitForFences(demo->device, 1, &demo->render_fence, VK_TRUE, UINT64_MAX);
    if (result != VK_SUCCESS) {
        err_log("vkWaitForFences failed: %d\n", result);
        goto fail;
    }

    if (!vk_render_create(demo, vma[i], &cursor_src, width, height)) {
        goto fail;
    }

    if (!vk_render_dst_create(demo, vma[i], &cursor_dst, demo->cursor_render_pass, target_width, target_height)) {
        goto fail;
    }

    // NOTE: we are using the same fence every frame anyway so we only really need one command buffer;
    // in case we change to one fence per swap chain image, make sure to change this to use its own command buffer
    // as well.
    VkCommandBuffer cmd = demo->command_buffers[demo->image_index];
    VkCommandBufferBeginInfo cmd_beg_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    cmd_beg_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    result = vkBeginCommandBuffer(cmd, &cmd_beg_info);

    if (result != VK_SUCCESS) {
        err_log("vkBeginCommandBuffer failed: %d\n", result);
        goto fail;
    }

    vk_render_upload_and_gen_mip_maps(cmd, vma[i], &cursor_src, base2, width, height);

    VkImageSubresourceRange range = {};
    range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    range.levelCount = 1;
    range.layerCount = 1;

    VkImageSubresourceRange range_mip = range;
    range_mip.levelCount = cursor_src.src_mip;

    VkImageMemoryBarrier barrier[BARRIER_COUNT] = {};
    barrier[BARRIER_SRC].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier[BARRIER_SRC].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier[BARRIER_SRC].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier[BARRIER_SRC].image = cursor_src.src.img;
    barrier[BARRIER_SRC].subresourceRange = range_mip;
    barrier[BARRIER_SRC].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier[BARRIER_SRC].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier[BARRIER_SRC].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier[BARRIER_SRC].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier[BARRIER_DST].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier[BARRIER_DST].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier[BARRIER_DST].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier[BARRIER_DST].image = cursor_dst.dst.img;
    barrier[BARRIER_DST].subresourceRange = range;
    barrier[BARRIER_DST].srcAccessMask = 0;
    barrier[BARRIER_DST].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier[BARRIER_DST].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier[BARRIER_DST].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, NULL, 0, NULL, BARRIER_COUNT,
        barrier);

    VkClearValue clear_color = {};
    VkRenderPassBeginInfo render_pass_info = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
    render_pass_info.renderPass = demo->cursor_render_pass;
    render_pass_info.framebuffer = cursor_dst.dst_view.fb;
    render_pass_info.renderArea.offset.x = 0;
    render_pass_info.renderArea.offset.y = 0;
    render_pass_info.renderArea.extent.width = target_width;
    render_pass_info.renderArea.extent.height = target_height;
    render_pass_info.clearValueCount = 1;
    render_pass_info.pClearValues = &clear_color;

    vkCmdBeginRenderPass(cmd, &render_pass_info,
                         VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(
        cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
        demo->cursor_pipeline);
    vkCmdBindDescriptorSets(
        cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
        demo->pipeline_layout, 0, 1,
        &cursor_src.src_view.desc, 0, NULL);
    VkViewport viewport;
    VkRect2D scissor;
    memset(&viewport, 0, sizeof(VkViewport));
    memset(&scissor, 0, sizeof(VkRect2D));
    scissor.offset.x = viewport.x = 0.0f;
    scissor.offset.y = viewport.y = 0.0f;
    scissor.extent.width = viewport.width = (float)target_width;
    scissor.extent.height = viewport.height = (float)target_height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRenderPass(cmd);

    VkImageSubresourceLayers layer = {};
    layer.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    layer.layerCount = 1;
    VkOffset3D offset = { 0, 0, 0 };
    VkExtent3D extent = { target_width, target_height, 1 };
    VkBufferImageCopy region = {};
    region.imageSubresource = layer;
    region.imageOffset = offset;
    region.imageExtent = extent;

    vkCmdCopyImageToBuffer(cmd, cursor_dst.dst.img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, cursor_dst.staging.buf, 1, &region);

    result = vkEndCommandBuffer(cmd);
    if (result != VK_SUCCESS) {
        err_log("vkEndCommandBuffer failed: %d\n", result);
        goto fail;
    }

    VkSubmitInfo submit_info = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &cmd;

    result = vkResetFences(demo->device, 1, &demo->render_fence);
    if (result != VK_SUCCESS) {
        err_log("vkResetFences failed: %d\n", result);
        goto fail;
    }

    result = vkQueueSubmit(demo->graphics_queue, 1, &submit_info,
                           demo->render_fence);

    if (result != VK_SUCCESS) {
        err_log("vkQueueSubmit failed: %d\n", result);
        goto fail;
    }

    result = vkWaitForFences(demo->device, 1, &demo->render_fence, VK_TRUE, UINT64_MAX);
    if (result != VK_SUCCESS) {
        err_log("vkWaitForFences failed: %d\n", result);
        goto fail;
    }

    result = vmaCopyAllocationToMemory(vma[i], cursor_dst.staging.alloc, 0, image_base2, target_width * target_height * GL_CHANNELS_N);
    if (result != VK_SUCCESS) {
        err_log("vmaCopyAllocationToMemory failed: %d\n", result);
        goto fail;
    }

    fail = false;
    for (int x = 0; x < target_width; ++x) {
        for (int y = 0; y < target_height; ++y) {
            int image_i = (y * target_width + x) * channels;
            int image2_i = (y * target_width + x) * GL_CHANNELS_N;
            image->image[image_i + 2] = image->image[image_i + 1] = image->image[image_i] =
                ((int)image_base2[image2_i] + (int)image_base2[image2_i + 2]) / 2;
            image->image[image_i + 3] = image_base2[image2_i + 1];
        }
    }

    image->width = target_width;
    image->height = target_height;
    image->channels = channels;

fail:
    free(image_base2);
fail_image_base2:
    free(base2);
fail_base2:
    if (fail) {
        free(image->image);
        image->image = 0;
    }
}

static void vk_render_destroy(struct vulkan_demo *demo, VmaAllocator vma, struct vk_render_src_t *render) {
    if (render->staging.buf) {
        vmaDestroyBuffer(vma, render->staging.buf, render->staging.alloc);
        render->staging = (struct vk_buffer_t){};
    }
    // do not destroy descriptor set, update it for reuse instead.
    if (render->src_view.view) {
        vkDestroyImageView(demo->device, render->src_view.view, NULL);
        render->src_view.view = NULL;
    }
    if (render->src.img) {
        vmaDestroyImage(vma, render->src.img, render->src.alloc);
        render->src = (struct vk_image_t){};
    }
}

static void vk_render_dst_destroy(struct vulkan_demo *demo, VmaAllocator vma, struct vk_render_dst_t *render) {
    if (render->staging.buf) {
        vmaDestroyBuffer(vma, render->staging.buf, render->staging.alloc);
        render->staging = (struct vk_buffer_t){};
    }
    if (render->dst_view.fb) {
        vkDestroyFramebuffer(demo->device, render->dst_view.fb, NULL);
        render->dst_view.fb = 0;
    }
    if (render->dst_view.view) {
        vkDestroyImageView(demo->device, render->dst_view.view, NULL);
        render->dst_view.view = NULL;
    }
    if (render->dst.img) {
        vmaDestroyImage(vma, render->dst.img, render->dst.alloc);
        render->dst = (struct vk_image_t){};
    }
}

static void vk_render_img_destroy(struct vulkan_demo *demo, VmaAllocator vma, struct vk_render_img_t *render) {
    // do not destroy descriptor set, update it for reuse instead.
    if (render->view.view) {
        vkDestroyImageView(demo->device, render->view.view, NULL);
        render->view.view = NULL;
    }
    if (render->img.img) {
        vmaDestroyImage(vma, render->img.img, render->img.alloc);
        render->img = (struct vk_image_t){};
    }
}

static void vmaAuxCleanup(void) {
    for (int j = 0; j < SCREEN_COUNT; ++j) {
        for (int i = 0; i < SCREEN_COUNT; ++i) {
            vk_render_destroy(&vk_demo[j], vma[j], &vk_render[j][i]);
            vk_render_img_destroy(&vk_demo[j], vma[j], &vk_render_upscaled[j][i]);
        }
    }
    vk_render_destroy(&vk_demo[SCREEN_TOP], vma[SCREEN_TOP], &cursor_src);
    vk_render_dst_destroy(&vk_demo[SCREEN_TOP], vma[SCREEN_TOP], &cursor_dst);
}

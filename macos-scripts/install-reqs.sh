#!/usr/bin/env bash

sudo ./vulkansdk-macOS-$VULKAN_VERSION.app/Contents/MacOS/vulkansdk-macOS-$VULKAN_VERSION --root ~/VulkanSDK/$VULKAN_VERSION --accept-licenses --default-answer --confirm-command install com.lunarg.vulkan.core com.lunarg.vulkan.usr

brew update
brew install sdl3 libplacebo

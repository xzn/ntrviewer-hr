#!/usr/bin/env bash

brew update
brew install sdl3 libplacebo wget

wget https://sdk.lunarg.com/sdk/download/$VULKAN_VERSION/mac/vulkansdk-macos-$VULKAN_VERSION.zip
unzip vulkansdk-macos-$VULKAN_VERSION.zip
sudo ./vulkansdk-macOS-$VULKAN_VERSION.app/Contents/MacOS/vulkansdk-macOS-$VULKAN_VERSION --root ~/VulkanSDK/$VULKAN_VERSION --accept-licenses --default-answer --confirm-command install com.lunarg.vulkan.core com.lunarg.vulkan.usr

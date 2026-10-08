#!/usr/bin/env bash

set -ex

ARCH=$(uname -m)

if [[ "$ARCH" == "arm64" ]]; then
    brew update
    brew install sdl3 libplacebo nasm
else
    URL="https://github.com/macports/macports-base/releases/download/v2.12.6"
    MACPORTS_PKG="MacPorts-2.12.6-26-Tahoe.pkg"
    curl -LO $URL/$MACPORTS_PKG
    sudo installer -verbose -pkg $MACPORTS_PKG -target /
    MACPORTS_PREFIX=/opt/local
    export PATH="$MACPORTS_PREFIX/bin:$PATH"
    echo "$MACPORTS_PREFIX/bin" >> "$GITHUB_PATH"
    sudo port install sdl3 libplacebo nasm
fi

wget -nv https://sdk.lunarg.com/sdk/download/$VULKAN_VERSION/mac/vulkansdk-macos-$VULKAN_VERSION.zip
unzip vulkansdk-macos-$VULKAN_VERSION.zip
./vulkansdk-macOS-$VULKAN_VERSION.app/Contents/MacOS/vulkansdk-macOS-$VULKAN_VERSION --root ~/VulkanSDK/$VULKAN_VERSION --accept-licenses --default-answer --confirm-command install com.lunarg.vulkan.core com.lunarg.vulkan.usr copy_only=1

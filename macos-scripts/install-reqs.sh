#!/usr/bin/env bash

set -ex

ARCH=$(uname -m)

URL="https://github.com/macports/macports-base/releases/download/v2.12.6"
export MACOSX_DEPLOYMENT_TARGET=15.0
MACPORTS_PKG="MacPorts-2.12.6-15-Sequoia.pkg"
curl -LO $URL/$MACPORTS_PKG
sudo installer -verbose -pkg $MACPORTS_PKG -target /
MACPORTS_PREFIX=/opt/local
export PATH="$MACPORTS_PREFIX/bin:$PATH"
echo "$MACPORTS_PREFIX/bin" >> "$GITHUB_PATH"
sudo port install sdl3 nasm
sudo port install libplacebo +vulkan +glslang

wget -nv https://sdk.lunarg.com/sdk/download/$VULKAN_VERSION/mac/vulkansdk-macos-$VULKAN_VERSION.zip
unzip vulkansdk-macos-$VULKAN_VERSION.zip
./vulkansdk-macOS-$VULKAN_VERSION.app/Contents/MacOS/vulkansdk-macOS-$VULKAN_VERSION --root ~/VulkanSDK/$VULKAN_VERSION --accept-licenses --default-answer --confirm-command install com.lunarg.vulkan.core com.lunarg.vulkan.usr copy_only=1

source ~/VulkanSDK/$VULKAN_VERSION/setup-env.sh
export PKG_CONFIG_PATH="/opt/local/lib/pkgconfig:/opt/local/share/pkgconfig:$PKG_CONFIG_PATH"
export CPATH="/opt/local/include:$VULKAN_SDK/include:$CPATH"
export LIBRARY_PATH="/opt/local/lib:$VULKAN_SDK/lib:$LIBRARY_PATH"

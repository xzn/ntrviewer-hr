#!/usr/bin/env bash

set -ex

brew update
brew install sdl3 libplacebo wget rustup

rustup toolchain install nightly-2025-08-08
rustup default nightly-2025-08-08
pushd librashader/librashader-capi
cargo build --profile optimized --no-default-features --features runtime-vulkan,runtime-metal
popd
ln -s ../../librashader/include/librashader.h ntrviewer-hr/include/librashader.h
mkdir ntrviewer-hr/lib
ln -s ../../librashader/target/optimized/liblibrashader_capi.dylib ntrviewer-hr/lib/librashader.dylib

wget -nv https://sdk.lunarg.com/sdk/download/$VULKAN_VERSION/mac/vulkansdk-macos-$VULKAN_VERSION.zip
unzip vulkansdk-macos-$VULKAN_VERSION.zip
./vulkansdk-macOS-$VULKAN_VERSION.app/Contents/MacOS/vulkansdk-macOS-$VULKAN_VERSION --root ~/VulkanSDK/$VULKAN_VERSION --accept-licenses --default-answer --confirm-command install com.lunarg.vulkan.core com.lunarg.vulkan.usr copy_only=1

#!/usr/bin/env bash

set -ex

rustup toolchain install nightly-2025-08-08
rustup default nightly-2025-08-08
pushd librashader/librashader-capi
cargo build --profile optimized --no-default-features --features runtime-vulkan,runtime-metal
popd

#!/usr/bin/env bash

set -ex

rustup toolchain install nightly-2026-08-02
rustup default nightly-2026-08-02
pushd librashader/librashader-capi
cargo build --profile optimized --no-default-features --features runtime-vulkan,runtime-metal
popd

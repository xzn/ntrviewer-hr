#!/usr/bin/env bash

set -ex

rustup toolchain install nightly-2026-01-08
rustup default nightly-2026-01-08
pushd librashader/librashader-capi
cargo build --profile optimized --no-default-features --features runtime-opengl,runtime-vulkan
popd

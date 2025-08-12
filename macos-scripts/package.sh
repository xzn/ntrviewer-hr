#!/usr/bin/env bash

set -ex

mkdir NTRViewer-HR-macOS-Universal
pushd NTRViewer-HR-macOS-Universal
cp ../ntrviewer-hr/macos-scripts/ntrviewer .
chmod 755 ntrviewer
mkdir -p NTRViewer-HR.app/Contents/MacOS
lipo -create ../macos/ntrviewer ../macos-intel/ntrviewer -output NTRViewer-HR.app/Contents/MacOS/ntrviewer
cp -R ../macos/arm64 NTRViewer-HR.app/Contents/MacOS/
cp -R ../macos-intel/x86_64 NTRViewer-HR.app/Contents/MacOS/
codesign --remove-signature NTRViewer-HR.app/Contents/MacOS/ntrviewer
codesign -s "JS Deck" NTRViewer-HR.app/Contents/MacOS/ntrviewer
codesign -vvv NTRViewer-HR.app/Contents/MacOS/ntrviewer
find . -name '*.dylib' -type f | xargs -L 1 codesign --remove-signature
find . -name '*.dylib' -type f | xargs -L 1 codesign -s "JS Deck"
find . -name '*.dylib' -type f | xargs -L 1 codesign -vvv
cp ../ntrviewer-hr/macos-scripts/Info.plist NTRViewer-HR.app/Contents
cp ../ntrviewer-hr/rashader.json NTRViewer-HR.app/Contents/MacOS
cp ../ntrviewer-hr/placebo.json NTRViewer-HR.app/Contents/MacOS
mkdir -p NTRViewer-HR.app/Contents/MacOS/placebo-shaders/Anime4K
cp -R ../Anime4K/glsl/* NTRViewer-HR.app/Contents/MacOS/placebo-shaders/Anime4K/
mkdir -p NTRViewer-HR.app/Contents/MacOS/slang-shaders
cp -R ../slang-shaders/{anti-aliasing,edge-smoothing,interpolation,pixel-art-scaling,stock.slang} NTRViewer-HR.app/Contents/MacOS/slang-shaders/
popd
ntrviewer-hr/macos-scripts/compress.sh

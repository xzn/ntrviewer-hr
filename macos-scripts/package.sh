#!/usr/bin/env bash

set -ex

rcodesign generate-self-signed-certificate --person-name "JS Deck" --country-name "CA" --validity-days 7300 --p12-file cert.p12 --p12-password password

mkdir NTRViewer-HR-macOS-Universal
pushd NTRViewer-HR-macOS-Universal
cp ../ntrviewer-hr/macos-scripts/ntrviewer .
chmod 755 ntrviewer
mkdir -p NTRViewer-HR.app/Contents/MacOS
lipo -create ../macos-app/ntrviewer ../macos-app-intel/ntrviewer -output NTRViewer-HR.app/Contents/MacOS/ntrviewer
codesign --remove-signature NTRViewer-HR.app/Contents/MacOS/ntrviewer
rcodesign sign --p12-file ../cert.p12 --p12-password=password NTRViewer-HR.app/Contents/MacOS/ntrviewer
codesign -vvv NTRViewer-HR.app/Contents/MacOS/ntrviewer
cp -R ../macos-app/arm64 NTRViewer-HR.app/Contents/MacOS/
cp -R ../macos-app-intel/x86_64 NTRViewer-HR.app/Contents/MacOS/
find . -name '*.dylib' -type f | xargs -L 1 codesign --remove-signature
find . -name '*.dylib' -type f | xargs -L 1 rcodesign sign --p12-file ../cert.p12 --p12-password=password
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

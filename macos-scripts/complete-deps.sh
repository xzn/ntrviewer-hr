#!/usr/bin/env bash

set -ex

BIN_DIR=../ntrviewer-hr
BIN=ntrviewer
ARCH=$(uname -m)

process () {
    echo Processing "$2" for "$1"
    SRC="$2"
    LIB="$(basename $SRC)"
    if [[ "$LIB" =~ lib(lib.+)_capi.dylib ]]; then
        LIB="${BASH_REMATCH[1]}.dylib"
    fi
    DST="$ARCH/$LIB"
    if [[ "$SRC" =~ ^/opt/.* || "$SRC" =~ ^/Users/.* ]]; then
        echo Third-party "$SRC"
        if [ ! -f "$DST" ]; then
            cp "$SRC" "$DST"
            codesign --remove-signature "$DST"
            codesign -s "-" "$DST"
            ( pre_process $DST )
            install_name_tool -id "@rpath/$LIB" "$DST"
            codesign --remove-signature "$DST"
            codesign -s "-" "$DST"
            chmod 644 "$DST"
        fi
        install_name_tool -change "$SRC" "@rpath/$LIB" "$1"
    elif [[ "$SRC" =~ ^/System/.* || "$SRC" =~ ^/usr/.* ]]; then
        echo System "$SRC"
    else
        echo Unknown "$SRC"
    fi
}

pre_process () {
    echo Preprocessing $1
    OTOOL_L="$(otool -L $1)"
    while IFS= read -r LINE; do
        PAT='^[[:space:]]*(.+)[[:space:]]+\(compatibility version .+, current version .+\)$'
        if [[ "$LINE" =~ $PAT ]]; then
            ( process "$1" "${BASH_REMATCH[1]}" )
        else
            echo Skipping $LINE
        fi
    done <<< "$OTOOL_L"
}

echo Binary "$BIN" on "$ARCH"
rm "$BIN"
cp "$BIN_DIR/$BIN" "$BIN"
rm -rf "$ARCH"
mkdir -p "$ARCH"
codesign --remove-signature "$BIN"
codesign -s "-" "$BIN"
( pre_process $BIN )
install_name_tool -rpath /opt/homebrew/lib @executable_path/$ARCH "$BIN"
codesign --remove-signature "$BIN"
codesign -s "-" "$BIN"

ICD=MoltenVK_icd.json
cp ~/VulkanSDK/$VULKAN_VERSION/macOS/share/vulkan/icd.d/$ICD $ARCH
BAK=.bak
sed -i $BAK 's/..\/..\/..\/lib/./g' $ARCH/$ICD
rm $ARCH/$ICD$BAK
MVK=libMoltenVK.dylib
cp ~/VulkanSDK/$VULKAN_VERSION/macOS/lib/$MVK $ARCH
lipo $ARCH/$MVK -extract arm64 -output $ARCH/$MVK
chmod 644 $ARCH/$MVK

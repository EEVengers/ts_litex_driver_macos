#!/bin/bash

set -e 

CONFIGURATION=Debug

xcodebuild -alltargets -configuration $CONFIGURATION

codesign -s - -f --entitlements "litepcie/litepcie.entitlements" "build/$CONFIGURATION/litepcie-manager.app/Contents/Library/SystemExtensions/litex.litepcie.dext"
codesign -s - -f --entitlements "litepcie-manager/litepcie_manager.entitlements" "build/$CONFIGURATION/litepcie-manager.app"
codesign -s - -f --entitlements "litepcie-client/litepcie_client.entitlements" "build/$CONFIGURATION/litepcie-client.app"

clang litepcie_util.c -o build/$CONFIGURATION/litepcie_util -I liblitepcie/ -I litepcie -lm -lliblitepcie -L build/$CONFIGURATION

codesign -s - -f --entitlements "litepcie-client/litepcie_client.entitlements" build/$CONFIGURATION/litepcie_util

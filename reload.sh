#!/bin/bash

set -e 

CONFIGURATION=Debug

systemextensionsctl reset
sleep 1
./build/$CONFIGURATION/litepcie-manager.app/Contents/MacOS/litepcie-manager forceActivate
sleep 1
./build/$CONFIGURATION/litepcie-manager.app/Contents/MacOS/litepcie-manager forceActivate

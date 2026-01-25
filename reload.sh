#!/bin/bash

set -e 

CONFIGURATION=Debug

systemextensionsctl reset
sleep 1
./build/$CONFIGURATION/thunderscope-manager.app/Contents/MacOS/thunderscope-manager forceActivate
sleep 1
./build/$CONFIGURATION/thunderscope-manager.app/Contents/MacOS/thunderscope-manager forceActivate

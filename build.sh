#!/usr/bin/env bash
# Builds arachne-cli (the tracer) on Linux: cmake and a C++17 compiler. The window and painting need Windows for now.
set -e
cd "$(dirname "$0")"
cmake -B build -DCMAKE_BUILD_TYPE=Release -DARACHNE_GUI=OFF
cmake --build build -j
echo "Built: build/arachne-cli"

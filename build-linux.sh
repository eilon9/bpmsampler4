#!/bin/bash
set -e

# Install JUCE build dependencies (run once)
if [[ "$1" == "--install-deps" ]]; then
    sudo apt-get update
    sudo apt-get install -y \
        build-essential cmake git \
        libasound2-dev \
        libx11-dev libxext-dev libxrandr-dev libxinerama-dev libxcursor-dev libxcomposite-dev \
        libfreetype6-dev \
        libfontconfig1-dev \
        libgl1-mesa-dev libglu1-mesa-dev \
        libharfbuzz-dev \
        libjack-jackd2-dev
    echo "Dependencies installed."
    exit 0
fi

# Configure (only needed once, or after CMakeLists changes)
if [[ "$1" == "--configure" || ! -d build ]]; then
    cmake -B build -DCMAKE_BUILD_TYPE=Release
fi

# Build
cmake --build build -- -j$(nproc)

echo ""
echo "Standalone: build/BPMSampler3_artefacts/Release/Standalone/BPMSampler3"
echo "VST3:       build/BPMSampler3_artefacts/Release/VST3/BPMSampler3.vst3"

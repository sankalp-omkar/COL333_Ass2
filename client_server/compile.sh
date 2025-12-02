#!/bin/bash
set -e

# Determine pybind11 CMake path
PYBIND11_DIR=$(python3 -m pybind11 --cmakedir)

# Make build directory
mkdir -p build
cd build

# Run CMake
cmake .. \
  -Dpybind11_DIR="$PYBIND11_DIR" \
  -DCMAKE_C_COMPILER=gcc \
  -DCMAKE_CXX_COMPILER=g++

# Build
make -j"$(nproc)"

# Return to project root
cd ..

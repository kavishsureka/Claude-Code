#!/bin/sh
#
# Build and Run script for Claude Code App
#

set -e # Exit early if any commands fail

(
  cd "$(dirname "$0")"
  
  # Configure cmake using vcpkg toolchain if VCPKG_ROOT is available
  if [ -n "$VCPKG_ROOT" ]; then
    cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake
  else
    cmake -B build -S .
  fi
  
  # Build the project
  cmake --build ./build
)

# Load environment variables from .env if it exists
if [ -f "$(dirname "$0")/.env" ]; then
  # export variables ignoring comments, removing spaces around =
  set -a
  source "$(dirname "$0")/.env"
  set +a
fi

# Run the executable
exec "$(dirname "$0")"/build/claude-code "$@"

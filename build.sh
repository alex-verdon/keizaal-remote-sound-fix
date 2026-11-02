#!/usr/bin/env sh
# Compile RemoteSoundFix.dll depuis Linux, macOS ou Windows (WSL) avec zig.
#   pip install ziglang   (ou installer zig depuis https://ziglang.org)
set -e
mkdir -p build
python3 -m ziglang cc -target x86_64-windows-gnu -O2 -shared -s \
  -o build/RemoteSoundFix.dll src/RemoteSoundFix.c
echo "OK : build/RemoteSoundFix.dll"

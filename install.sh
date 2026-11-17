#!/usr/bin/env bash
# Installs arachne-cli for this user on Linux (~/.local/bin). Build it first: ./build.sh
set -e
cd "$(dirname "$0")"
mkdir -p "$HOME/.local/bin"
cp build/arachne-cli "$HOME/.local/bin/"
echo "Installed: $HOME/.local/bin/arachne-cli"

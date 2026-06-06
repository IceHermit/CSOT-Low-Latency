#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 3 ]; then
    echo "Usage: $0 <input_file> <output_file> <include_directory>"
    exit 1
fi

if [ ! -f "$1" ]; then
    echo "'$1' is not a file (or does not exist)."
    exit 1
fi

g++ -std=c++20 -O3 -shared -fPIC -flto -ffast-math -fomit-frame-pointer -Wall -Wextra -Wpedantic -I"$3" -o "$2" "$1"

echo "Built $2\n"
file "$2"
echo "\n"

nm -D "$2" | grep -F create_strategy || {
  echo "ERROR: create_strategy not exported" >&2
  exit 1
}

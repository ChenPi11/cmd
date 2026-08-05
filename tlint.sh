#!/bin/bash

set -e

if [ $# -ne 1 ]; then
    echo "Usage: $0 <file>"
    exit 1
fi

file="$1"

filename=$(basename "$file")
if [ ${#filename} -gt 12 ]; then
    echo "Filename name is too long, please use 8.3 format: $filename"
    exit 1
fi

last_byte=$(tail -c 1 "$file" | xxd -p)
if [ ! "$last_byte" = "0a" ]; then
    echo "File does not end with a newline: $file"
    exit 1
fi

if [ ! "$(file -b --mime-encoding "$file")" = "us-ascii" ]; then
    echo "File is not ASCII: $file"
    exit 1
fi

echo -e "\x1b[32mPASS:\x1b[0m $file"

exit 0

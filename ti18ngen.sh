#!/bin/bash

# Generate i18n headers.

set -e

LANGS=(
    en_us # American English
    zh_cn # Simplified Chinese
    zh_ms # Microsoft Chinese
    zh_wy # Ancient Chinese
)

for lang in "${LANGS[@]}"; do
    ./ti18ntbl.py UTF-8 < "i$lang.txt" > "i$lang.h"
done

./ti18nlst.py "${LANGS[@]}" > "ilanguages.h"

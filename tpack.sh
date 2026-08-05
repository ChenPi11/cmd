#!/bin/bash

# Pack files into a single archive.

set -e

VERSION=0.1.0

./ti18ngen.sh

FILELIST=(
    *.c
    *.h
    AUTOEXEC.BAT
    README
    LICENSE
    Makefile
    tbuild.sh
)

for file in "${FILELIST[@]}"; do
    ./tlint.sh "$file"
done

rm -rf /tmp/cmd-$VERSION
mkdir -p /tmp/cmd-$VERSION

cp -a "${FILELIST[@]}" /tmp/cmd-$VERSION

rm -f cmd.tar cmd.tar.xz cmd.a

# Tar archive.
pushd /tmp
tar -o -cf cmd.tar --owner=0 --group=0 --mtime='@0' --numeric-owner cmd-$VERSION
popd
mv /tmp/cmd.tar cmd.tar

# Compressed tar archive.
xz -c -9 cmd.tar > cmd.tar.xz

# Ar archive.
llvm-ar --format=gnu crD cmd.gnu.a "${FILELIST[@]}"
llvm-ar --format=darwin crD cmd.darwin.a "${FILELIST[@]}"
llvm-ar --format=bsd crD cmd.bsd.a "${FILELIST[@]}"
llvm-ar --format=bigarchive crD cmd.aix.a "${FILELIST[@]}"
llvm-ar --format=coff crD cmd.coff.a "${FILELIST[@]}"

# UU'ed tar archive.
uuencode cmd.tar cmd.tar > cmd.tar.uu

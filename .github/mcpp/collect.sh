#!/bin/sh
# collect.sh TARGET: a built target's files for a release, in rel/ (not dist/,
# which mcpp pack empties):
#   rel/bare/<target>-<file>  the programs as built (the form of the project's releases)
#   rel/pack/                 mcpp's package of the target (both programs, README, LICENSE)
# The web page is the package of wasm32-emscripten (the web job). A DJGPP program
# is static by construction; mcpp packs it as such only when told (mcpp#1007).
set -eu
t=$1
mkdir -p rel/bare rel/pack
if [ "$t" = openkal ]; then
  cp target/dist/cmd-*-openkal-bundle-all.tar.gz rel/pack/
  exit 0
fi
# target/<target>/<build>/bin/<program>: the two programs, not a step's intermediates
# nor the run-time libraries mcpp places beside them (those are in the package).
find "target/$t" -mindepth 3 -maxdepth 3 -path '*/bin/*' -type f \( -name 'cmd.exe*' -o -name 'COMMAND.COM*' \) |
while read -r f; do
  cp "$f" "rel/bare/$t-$(basename "$f")"
done
case $t in
  wasm32-emscripten) ;;
  i586-dos-djgpp) mode='--mode static' ;;
  *) mode= ;;
esac
if [ "$t" != wasm32-emscripten ]; then
  # shellcheck disable=SC2086
  mcpp pack --release --target "$t" --all-programs $mode -o rel/pack/ ||
    echo "::warning::$t: mcpp pack failed; the release has its programs as built only"
fi
ls -l rel/bare rel/pack

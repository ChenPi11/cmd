#!/bin/sh
# check.sh OUTPUT ROW: grades a run of test.bat by its levels (test.bat). Each part
# of OUTPUT is compared with its part of expected.txt, ignoring carriage returns,
# colour escapes and empty lines, and case with CHECK_FOLD_CASE=1 (DOS without
# long names keeps names in upper case). The run fails when a part up to the
# row's floor (floor.txt) differs; the level it reaches is reported either way.
here=$(cd "$(dirname "$0")" && pwd)
out=$1 row=$2
floor=$(awk -v r="$row" '$1 == r { print $2 }' "$here/floor.txt")
[ -n "$floor" ] || { echo "::error::check.sh: $row has no floor in floor.txt"; exit 1; }
esc=$(printf '\033')
norm() {
  tr -d '\r' < "$1" | sed "s/${esc}\[[0-9;]*m//g" | grep -v '^[[:space:]]*$' |
    if [ "${CHECK_FOLD_CASE:-0}" = 1 ]; then tr '[:upper:]' '[:lower:]'; else cat; fi
}
# part FILE N: the lines of part L<N> of a normalized FILE.
part() { awk -v n="-- l$2" 'tolower($0) ~ /^-- l[0-9]$/ { on = (tolower($0) == n); next } on' "$1"; }
norm "$here/expected.txt" > "$out.expected"
norm "$out" > "$out.norm"
reached=none status=0
for n in 0 1 2; do
  part "$out.expected" $n > "$out.L$n.expected"
  part "$out.norm" $n > "$out.L$n"
  if [ "$reached" = "L$((n - 1))" ] || [ $n = 0 ]; then
    cmp -s "$out.L$n.expected" "$out.L$n" && reached=L$n
  fi
  if [ $n -le "${floor#L}" ] && [ "$reached" != "L$n" ]; then
    echo "::error::$row: part L$n of test.bat differs"
    diff -u "$out.L$n.expected" "$out.L$n"
    status=1
  fi
done
line="$row: reaches $reached (floor $floor)"
[ "$reached" = "$floor" ] || [ $status = 1 ] || line="$line -- above its floor"
echo "$line"
[ -z "${GITHUB_STEP_SUMMARY:-}" ] || echo "$line" >> "$GITHUB_STEP_SUMMARY"
exit $status

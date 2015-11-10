# path.p.tst: test of pathname handling for any POSIX-compliant shell
# vim: set ft=sh ts=8 sts=4 sw=4 noet:

tmp=${TESTTMP}/path.p.tmp

echo ===== hash builtin =====

PATH= hash 2>/dev/null


cd "$TESTTMP"
rm -fr "$tmp"

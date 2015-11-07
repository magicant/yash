# path.p.tst: test of pathname handling for any POSIX-compliant shell
# vim: set ft=sh ts=8 sts=4 sw=4 noet:

tmp=${TESTTMP}/path.p.tmp

echo ===== umask builtin =====

umask=$(umask)
umask 0
umask a-r
umask ugo+r
umask u-rw,u-x
umask go+x=u
umask -S
umask "${umask}"
[ x"${umask}" = x"$(umask)" ] && echo ok

echo ===== hash builtin =====

PATH= hash 2>/dev/null


cd "$TESTTMP"
rm -fr "$tmp"

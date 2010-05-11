# path.p.tst: test of pathname handling for any POSIX-compliant shell
# vim: set ft=sh ts=8 sts=4 sw=4 noet:

tmp=${TESTTMP}/path.p.tmp

echo ===== pathname expansion =====

if [ "$(id -u)" -eq 0 ]; then
    # cannot test as root because file permissions are ignored
    cat <<END
1 pathexp/unreadable/file
2 pathexp/unreadable/file
3 pathexp/unreadable/file
4 pathexp/unreadable/file
5 pathexp/un*able/f*le
6 pathexp/unreadable/f*le
7 pathexp/un*able/file
8 pathexp/un*able/f*le
9 pathexp/unreadable/f*le
END
else (
mkdir -p "$tmp/pathexp/unreadable"
cd "$tmp"
>"pathexp/unreadable/file"
echo 1 pathexp/un*able/file
echo 2 pathexp/un*able/f*le
echo 3 pathexp/unreadable/f*le
chmod a-r pathexp/unreadable
echo 4 pathexp/un*able/file
echo 5 pathexp/un*able/f*le
echo 6 pathexp/unreadable/f*le
chmod a-x pathexp/unreadable
echo 7 pathexp/un*able/file
echo 8 pathexp/un*able/f*le
echo 9 pathexp/unreadable/f*le
chmod a+rx pathexp/unreadable
) fi


echo ===== cd builtin =====

HOME=/
ORIGPWD=$PWD
mkdir -p "$tmp/dir/dir"
if [ x"$(cd; echo $PWD)" = x"$HOME" ]; then
    echo cd \$HOME
fi
cd "$tmp"
if [ x"$ORIGPWD" = x"$OLDPWD" ]; then
    echo cd \$OLDPWD
fi
cd -- - >"$tmp/path.p.tmp"
if [ x"$PWD" = x"$ORIGPWD" ] && [ x"$PWD" = x"$(cat "$tmp/path.p.tmp")" ]; then
    echo cd \$PWD
fi

ln -s . "$tmp/path.p.link"
cd -LP "$tmp/path.p.link"
if [ x"$PWD" = x"$(pwd -P)" ]; then
    echo cd -P
fi
cd -PL "$tmp/path.p.link"
if [ x"$PWD" = x"$tmp/path.p.link" ] && [ x"$PWD" = x"$(pwd)" ]; then
    echo cd -L
fi
cd "$tmp/path.p.link"
if [ x"$PWD" = x"$tmp/path.p.link" ] && [ x"$PWD" = x"$(pwd)" ]; then
    echo cd -L default
fi

cd "$tmp"
CDPATH=$tmp/dir/dir:$tmp/dir cd dir >"$tmp/path.p.tmp"
if [ x"$PWD" = x"$tmp/dir/dir" ] && [ x"$PWD" = x"$(cat "$tmp/path.p.tmp")" ]
then
    echo cd \$CDPATH 1
fi
cd ../..
mv "$tmp/dir/dir" "$tmp/dir/dir2"
CDPATH=$tmp/dir:$tmp/dir/dir2 cd dir
if [ x"$PWD" = x"$tmp/dir" ]; then
    echo cd \$CDPATH 2
fi

echo cd canonicalization "$(CDPATH=/ cd dev)"

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


rm -fr "$tmp"

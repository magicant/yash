echo ===== cd builtin =====

HOME=/
ORIGPWD=$PWD
mkdir -p "${TESTTMP}/dir/dir"
if [ x"$(cd; echo $PWD)" = x"$HOME" ]; then
	echo cd \$HOME
fi
cd "$TESTTMP"
if [ x"$ORIGPWD" = x"$OLDPWD" ]; then
	echo cd \$OLDPWD
fi
cd -- - >"${TESTTMP}/path.p.tmp"
if [ x"$PWD" = x"$ORIGPWD" ] && [ x"$PWD" = x"$(cat "${TESTTMP}/path.p.tmp")" ]
then
	echo cd \$PWD
fi

ln -s . "${TESTTMP}/path.p.link"
cd -LP "${TESTTMP}/path.p.link"
if [ x"$PWD" = x"$(pwd -P)" ]; then
	echo cd -P
fi
cd -PL "${TESTTMP}/path.p.link"
if [ x"$PWD" = x"${TESTTMP}/path.p.link" ] && [ x"$PWD" = x"$(pwd)" ]; then
	echo cd -L
fi
cd "${TESTTMP}/path.p.link"
if [ x"$PWD" = x"${TESTTMP}/path.p.link" ] && [ x"$PWD" = x"$(pwd)" ]; then
	echo cd -L default
fi

cd "${TESTTMP}"
CDPATH=${TESTTMP}/dir/dir:${TESTTMP}/dir cd dir >"${TESTTMP}/path.p.tmp"
if [ x"$PWD" = x"${TESTTMP}/dir/dir" ] &&
	[ x"$PWD" = x"$(cat "${TESTTMP}/path.p.tmp")" ]
then
	echo cd \$CDPATH 1
fi
cd ../..
mv "${TESTTMP}/dir/dir" "${TESTTMP}/dir/dir2"
CDPATH=${TESTTMP}/dir:${TESTTMP}/dir/dir2 cd dir
if [ x"$PWD" = x"${TESTTMP}/dir" ]; then
	echo cd \$CDPATH 2
fi

echo ===== umask builtin =====

umask=$(umask)
umask 0
umask a-r
umask ugo+r
umask u-rw,u-x
umask go=u
umask -S
umask "${umask}"
[ x"${umask}" = x"$(umask)" ] && echo ok


rm -fr "${TESTTMP}/path.p."* "${TESTTMP}/dir"

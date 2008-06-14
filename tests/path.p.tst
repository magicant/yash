tmp=/tmp/yashtest.$$

echo ===== cd builtin =====

HOME=/
ORIGPWD=$PWD
if [ x"$(cd; echo $PWD)" = x"$HOME" ]; then
	echo cd \$HOME
fi
cd /tmp
echo "\$PWD=$PWD"
if [ x"$ORIGPWD" = x"$OLDPWD" ]; then
	echo cd \$OLDPWD
fi
cd -- - >$tmp
if [ x"$PWD" = x"$ORIGPWD" ] && [ x"$PWD" = x"$(cat $tmp)" ]; then
	echo cd \$PWD
fi
rm -f $tmp
echo "\$OLDPWD=$OLDPWD"

ln -s . $tmp
cd $tmp
if [ x"$PWD" = x"$tmp" ]; then
	echo cd symlink
fi
cd -LP $tmp
if [ x"$PWD" = x"/tmp" ]; then
	echo cd -P
fi
cd -PL $tmp
if [ x"$PWD" = x"$tmp" ]; then
	echo cd -L
fi

rm -f $tmp
# TODO temporary file should be removed in EXIT trap

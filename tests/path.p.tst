tmp=/tmp/yashtest.$$

echo ===== cd builtin =====

HOME=/
ORIGPWD=$PWD
mkdir -p $tmp/dir/dir
if [ x"$(cd; echo $PWD)" = x"$HOME" ]; then
	echo cd \$HOME
fi
cd /tmp
echo "\$PWD=$PWD"
if [ x"$ORIGPWD" = x"$OLDPWD" ]; then
	echo cd \$OLDPWD
fi
cd -- - >$tmp/out
if [ x"$PWD" = x"$ORIGPWD" ] && [ x"$PWD" = x"$(cat $tmp/out)" ]; then
	echo cd \$PWD
fi
echo "\$OLDPWD=$OLDPWD"

ln -s . $tmp/link
cd -LP $tmp/link
if [ x"$PWD" = x"$(pwd -P)" ]; then
	echo cd -P
fi
cd -PL $tmp/link
if [ x"$PWD" = x"$tmp/link" ]; then
	echo cd -L
fi
cd $tmp/link
if [ x"$PWD" = x"$tmp/link" ]; then
	echo cd -L default
fi

cd $tmp
CDPATH=$tmp/dir/dir:$tmp/dir cd dir >$tmp/out
if [ x"$PWD" = x"$tmp/dir/dir" ] && [ x"$PWD" = x"$(cat $tmp/out)" ]; then
	echo cd \$CDPATH 1
fi
cd ../..
mv $tmp/dir/dir $tmp/dir/dir2
CDPATH=$tmp/dir:$tmp/dir/dir2 cd dir
if [ x"$PWD" = x"$tmp/dir" ]; then
	echo cd \$CDPATH 2
fi

rm -fr $tmp
# TODO temporary file should be removed in EXIT trap

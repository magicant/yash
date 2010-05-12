# dirstack.y.tst: yash-specific test of directory stack
# vim: set ft=sh ts=8 sts=4 sw=4 noet:

origpwd=$PWD
tmp=$TESTTMP/dirstack.y.tmp

mkdir "$tmp"
mkdir "$tmp/1" "$tmp/2" "$tmp/3" "$tmp/4"

cd "$tmp"
pushd "$tmp/1"; echo 1 $?; >f1
pushd "$tmp/2"; echo 2 $?; >f2
pushd "$tmp/3"; echo 3 $?; >f3
pushd "$tmp/4"; echo 4 $?; >f4
dirs >"$tmp/out"; echo 5 $?
diff - "$tmp/out" <<END
$tmp/4
$tmp/3
$tmp/2
$tmp/1
$tmp
END
dirs -v >"$tmp/out"; echo 6 $?
diff - "$tmp/out" <<END
+0	-4	$tmp/4
+1	-3	$tmp/3
+2	-2	$tmp/2
+3	-1	$tmp/1
+4	-0	$tmp
END
echo *; popd >/dev/null; echo 7 $?
echo *; popd >/dev/null; echo 8 $?
echo *; popd >/dev/null; echo 9 $?
echo *; popd >/dev/null; echo 10 $?

if [ x"$PWD" = x"$tmp" ]; then
    echo ok
fi
echo */*
pushd 1
dirs -c
echo $?
cd - >/dev/null
echo */*

echo ===== 1 =====

pushd "$tmp/1"
pushd "$tmp/2"
pushd "$tmp/3"
pushd "$tmp/4"
dirs | sed -e 's;/.*/;;'
dirs -v | sed -e 's;/.*/;;'

echo ===== 2 =====

pushd
dirs | sed -e 's;/.*/;;'
pushd
dirs | sed -e 's;/.*/;;'

echo ===== 3 =====

pushd +3
pushd -2
dirs | sed -e 's;/.*/;;'

echo ===== 4 =====

popd +1
popd -2
dirs | sed -e 's;/.*/;;'

echo ===== 5 =====

pushd +0
pushd -2
dirs | sed -e 's;/.*/;;'

echo ===== 6 =====

pushd --default-directory="$tmp/4" "$tmp/1"
pushd --default-directory="$tmp/4"
dirs | sed -e 's;/.*/;;'

echo ===== 7 =====

pushd --default-directory=+2
dirs | sed -e 's;/.*/;;'

echo ===== 8 =====

pushd "$tmp/1"
pushd "$tmp/2"
pushd "$tmp/1"
pushd "$tmp/2"
pushd --remove-duplicates "$tmp/1"
dirs | sed -e 's;/.*/;;'
echo =====
dirs -v +0 -0 | sed -e 's;/.*/;;'
dirs    +2 -2 | sed -e 's;/.*/;;'

echo ===== 9 =====

unset DIRSTACK
popd >/dev/null 2>/dev/null
echo empty popd $?

cd "$tmp"
(
cd "$tmp/1"
pushd - | sed -e 's;/.*/;;'
)
pushd --default-directory=- 2>/dev/null
echo pushd hyphen $?

pushd "$tmp/1"
readonly DIRSTACK
pushd "$tmp/2" 2>/dev/null
echo dirstack readonly $?
dirs | sed -e 's;/.*/;;'

cd "$origpwd"
rm -fr "$tmp"

echo ===== 10 =====

$INVOKE $TESTEE <<\END
pushd --no-such-option
echo pushd no-such-option $?
pushd ./no/such/dir 2>/dev/null
echo pushd no-such-dir $?
pushd /
pushd +5
echo pushd index out of range $?
pushd - >&- 2>/dev/null
echo pushd output error $?
END
$INVOKE $TESTEE <<\END
pushd /
popd --no-such-option
echo popd no-such-option $?
popd +5
echo popd index out of range $?
popd >&- 2>/dev/null
echo popd output error $?
popd
echo popd dirstack empty $?
END
$INVOKE $TESTEE <<\END
pushd /
dirs --no-such-option
echo dirs no-such-option $?
dirs +5
echo dirs index out of range $?
dirs >&- 2>/dev/null
echo dirs output error $?
END

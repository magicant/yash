# option.p.tst: test of shell options for any POSIX-compliant shell
# vim: set ft=sh ts=8 sts=4 sw=4 noet:

# XXX: test of the -m and -b options are not supported

tmp="${TESTTMP}/option.p.tmp"

echo ===== -a =====
set -a
echo "$-" | grep -Fq a || echo 'no a in $-' >&2
unset foo bar
foo=123
env | grep ^foo=
set +o allexport
bar=456
env | grep ^bar=

echo ===== -e =====
set -e
echo "$-" | grep -Fq e || echo 'no e in $-' >&2
false | (echo ok group; false; echo not reached) | cat
! false
if false; then echo if error
elif false; then echo elif error
else false || echo ok if
fi
i=0
while [ x"$i" != x"1" ]; do echo "$i"; i=1; done
i=0
until [ x"$i" = x"1" ]; do echo "$i"; i=1; done
set +o errexit
false
test $? -eq 0; echo 1-$?
$INVOKE $TESTEE -ce 'false; echo not reached'
test $? -eq 0; echo 2-$?
$INVOKE $TESTEE -ce '{ false; }; echo not reached'
test $? -eq 0; echo 3-$?
$INVOKE $TESTEE -ce '( false; ); echo not reached'
test $? -eq 0; echo 4-$?
$INVOKE $TESTEE -ce 'if true; then false; fi; echo not reached'
test $? -eq 0; echo 5-$?
$INVOKE $TESTEE -e <<\END
for i in 1 2 3
do
    echo a $i
    test $i -ne 2
    echo b $i
done
echo not reached
END
test $? -eq 0; echo 6-$?
$INVOKE $TESTEE -e <<\END
case i in (i)
    echo 7
    false
    echo not reached
esac
echo not reached
END
test $? -eq 0; echo 7-$?
$INVOKE $TESTEE -e <<\END
while true; do
    echo 8
    false
    echo not reached
done
echo not reached
END
test $? -eq 0; echo 8-$?
$INVOKE $TESTEE -e <<\END
until false; do
    echo 9
    false
    echo not reached
done
echo not reached
END
test $? -eq 0; echo 9-$?
$INVOKE $TESTEE -ce 'true && false; echo not reached'
test $? -eq 0; echo 10-$?
$INVOKE $TESTEE -ce 'false || false; echo not reached'
test $? -eq 0; echo 11-$?
$INVOKE $TESTEE -e <<\END
for i in i; do
    ! echo 12 # this should not exit
done
test $? -ne 0
echo 12-$?
END

echo ===== -f =====
set -f
echo "$-" | grep -Fq f || echo 'no f in $-' >&2
echo /*
set +o noglob

echo ===== -n =====
$INVOKE $TESTEE -n <<END
set +o noexec
echo ng noexec
END

echo ===== -u =====
set -u
echo "$-" | grep -Fq u || echo 'no u in $-' >&2
unset none
if (echo $none) 2>/dev/null; then echo ng nounset; else echo ok nounset; fi
set +o nounset
$INVOKE $TESTEE -u 2>/dev/null <<\END
$none
echo ng nounset 2
END

echo ===== -v =====
echo ===== -v ===== >&2
$INVOKE $TESTEE -v <<\END
echo "$-" | grep -Fq v || echo 'no v in $-' >&2
var=123
echo ${var#1}
END

echo ===== -x =====
echo ===== -x ===== >&2
$INVOKE $TESTEE -x <<\END
(echo "$-" | grep -Fq x) 2>/dev/null || echo 'no x in $-' >&2
var=123
echo ${var#1}
END
if ! { set -o xtrace && set +x; } 2>/dev/null
then
	echo set -o xtrace +x: ng
fi

echo ===== ignoreeof =====
# XXX We cannot actually test 'ignoreeof' since input from terminal is required.
#     Here we check if 'ignoreeof' is ignored in a non-interactive shell
$INVOKE $TESTEE -o ignoreeof <<\END
echo ignoreeof
END
echo ok

echo ===== -o +o =====
set -aeu
set -o > "$tmp"
saveset=$(set +o)
set +aeu
eval "$saveset"
set -o | diff "$tmp" - && echo ok
rm -f "$tmp"

# test of -C option is in "redir.p.tst"

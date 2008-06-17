echo ===== -a =====
set -a
# TODO unset foo bar
foo=123
env | grep ^foo=
set +o allexport
bar=456
env | grep ^bar=

echo ===== -e =====
set -e
(echo ok group; false; echo ng group)
if false; then echo ng if; else false || echo ok if; fi
i=0
until [ x"$i" = x"1" ]; do echo "$i"; i=1; done
set +o errexit

echo ===== -f =====
set -f
echo /*
set +o noglob

echo ===== -h =====
# TODO option: -h

echo ===== -m =====
echo ===== -b =====
# TODO option: -mb

echo ===== -n =====
$INVOKE $TESTEE -n <<END
set +o noexec
echo ng noexec
END

echo ===== -u =====
set -u
# TODO unset none
if (echo $none) 2>/dev/null; then echo ng nounset; else echo ok nounset; fi
set +o nounset
$INVOKE $TESTEE -u 2>/dev/null <<\END
$none
echo ng nounset 2
END

echo ===== -v =====
$INVOKE $TESTEE -v <<\END
var=123
echo ${var#1}
END

echo ===== -x =====
$INVOKE $TESTEE -x <<\END
var=123
echo ${var#1}
END
{
	set -o xtrace
	set +x
} 2>/dev/null

echo ===== ignoreeof =====
# XXX We cannot actually test 'ignoreeof' since input from terminal is required.
#     Here we check if 'ignoreeof' is ignored in a non-interactive shell
$INVOKE $TESTEE -o ignoreeof <<\END
echo ignoreeof
END
echo ok

# test of -C option is in "redir.p.tst"

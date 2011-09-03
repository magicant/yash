# redir.p.tst: test of redirections for any POSIX-compliant shell
# vim: set ft=sh ts=8 sts=4 sw=4 noet:

tmp="${TESTTMP}/redir.p.tmp"

echo Hello, >"$tmp"
cat "$tmp"

echo World. >>"$tmp"
cat <"$tmp"

1>&1 echo redirection before command

echo \2>"$tmp"
echo 2\>/dev/null|cat - "$tmp"

rm -f "$tmp"
echo foo > \
"$tmp"
[ -f "$tmp" ] && cat "$tmp"
set -C
echo bar >|"$tmp"
(echo baz >"$tmp") 2>/dev/null || echo noclobber
set +C
cat "$tmp"

echo ===== 1 =====

unset unset
2>&- echo 3>&1 complex 2>& ${unset--} redirection >& \3
<>/dev/null cat 2<& '0' 3>& 1
(</dev/null >&0) 2>/dev/null || echo not writable
(>/dev/null <&1) 2>/dev/null || echo not readable

exec 5>&1
exec >& -
echo exec redirect >&5
exec >&5 5>&-
echo exec redirect

exec 4<>/dev/null
cat >&4 <&4 # prints nothing

$INVOKE $TESTEE -c 'exec >&5; echo not printed' 2>/dev/null
$INVOKE $TESTEE -c 'command exec >&5; echo printed 1' 2>/dev/null
{ exec 4>&1; } 5>&1
echo exec in group >&4
{ echo error >&5; } 2>/dev/null || echo file descriptor closed

$INVOKE $TESTEE -c 'var="printed 2"; trap "echo \$var" EXIT >/dev/null'
$INVOKE $TESTEE -c '
settrap() {
    trap "echo printed 3" EXIT
    echo not printed
}
settrap >/dev/null'
$INVOKE $TESTEE -c '
{
    trap "echo printed 4" EXIT
    echo not printed
} >/dev/null'

echo ===== 2 =====

cat <<END >"$tmp"
Test of here-document.
		<"${TERM+}">
		"'"''\\-\'\"
END
cat "$tmp"

cat <<"END" >"$tmp"
Test of here-document.
		<"${TERM+}">
		"'"''\\-\'\"
END
cat "$tmp"

cat <<-\
END -
Test of here-document.
		<"${TERM+}">
		"'"''\\-\'\"
		END

cat <<-E\ ND -
Test of here-document.
		<"${TERM+}">
		"'"''\\-\'\"
		E ND

cat <<END
Test of here-document.
123\
456
END

cat <<\END
Test of here-document.
123\
456
END

cat <<END; cat <<EOF
1
END
2
EOF

rm -f "$tmp"

echo ===== 3 =====

# test of long here-document
(
    echo 'exec cat <<END'
    i=0
    while [ $i -lt 10000 ]; do
	printf '%d\n' \
	    $((i   )) $((i+ 1)) $((i+ 2)) $((i+ 3)) $((i+ 4)) $((i+ 5)) \
	    $((i+ 6)) $((i+ 7)) $((i+ 8)) $((i+ 9)) $((i+10)) $((i+11)) \
	    $((i+12)) $((i+13)) $((i+14)) $((i+15)) $((i+16)) $((i+17)) \
	    $((i+18)) $((i+19)) $((i+20)) $((i+21)) $((i+22)) $((i+23)) \
	    $((i+24)) $((i+25)) $((i+26)) $((i+27)) $((i+28)) $((i+29)) \
	    $((i+30)) $((i+31)) $((i+32)) $((i+33)) $((i+34)) $((i+35)) \
	    $((i+36)) $((i+37)) $((i+38)) $((i+39)) $((i+40)) $((i+41)) \
	    $((i+42)) $((i+43)) $((i+44)) $((i+45)) $((i+46)) $((i+47)) \
	    $((i+48)) $((i+49))
	i=$((i+50))
    done
    echo 'END'
) | $INVOKE $TESTEE | (
    i=0
    while read j; do
	if [ $i -ne $j ]; then echo "error: $j"; exit 1; fi
	i=$((i+1))
    done
    echo $i
)

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
$INVOKE $TESTEE -c 'command exec >&5; echo printed' 2>/dev/null
{ exec 4>&1; } 5>&1
echo exec in group >&4
{ echo error >&5; } 2>/dev/null || echo file descriptor closed

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
    while [ $i -lt 10000 ]; do echo $i; i=$((i+1)); done
    echo 'END'
) | $INVOKE $TESTEE | (
    i=0
    while read j
    do
	if [ $i -ne $j ]; then echo "error: $j"; exit 1; fi
	i=$((i+1))
    done
    echo $i
)

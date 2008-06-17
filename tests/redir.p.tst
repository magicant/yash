tmp=/tmp/yashtest.$$

echo Hello, >$tmp
cat $tmp

echo World. >>$tmp
cat <$tmp

1>&1 echo redirection before command

echo \2>$tmp
echo 2\>/dev/null|cat - $tmp

rm -f $tmp
echo foo >$tmp
[ -f $tmp ] && cat $tmp
set -C
(>$tmp) 2>/dev/null
set +C
cat $tmp

echo ===== 1 =====

# TODO unset unset
>&- echo 3>&2 complex 2>& ${unset--} redirection >& \3
<>/dev/null cat 2<& '0' 3>& 1
(</dev/null >&0) 2>/dev/null || echo not writable
(>/dev/null <&1) 2>/dev/null || echo not readable
# TODO complex redirections using 'exec'

echo ===== 2 =====

cat <<END >$tmp
Test of here-document.
		<"${TERM+}">
		"'"''\\-\'\"
END
cat $tmp

cat <<"END" >$tmp
Test of here-document.
		<"${TERM+}">
		"'"''\\-\'\"
END
cat $tmp

cat <<-END -
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

rm -f $tmp
# TODO temporary file should be removed in EXIT trap

temp="${TESTTMP}/redir"

echo ===== pipe redirection =====

exec 3>&- 4>&- 5>&- 6>&-
exec 4>>|3; echo 4-3 >&4; exec 4>&-; cat <&3; exec 3<&-
exec 4>>|6; echo 4-6 >&4; exec 4>&-; cat <&6; exec 6<&-
exec 5>>|3; echo 5-3 >&5; exec 5>&-; cat <&3; exec 3<&-
exec 5>>|6; echo 5-6 >&5; exec 5>&-; cat <&6; exec 6<&-
exec 5>>|4; echo 5-4 >&5; exec 5>&-; cat <&4; exec 4<&-
exec 3>>|6; echo 3-6 >&3; exec 3>&-; cat <&6; exec 6<&-
exec 3>>|4; echo 3-4 >&3; exec 3>&-; cat <&4; exec 4<&-

(cat >"$temp" | echo loop) >>|0
(while read i; do if [ $i -lt 5 ]; then echo $((i+1)); else exit; fi done |
{ echo 0; tee -a "$temp"; }) >>|0
cat "$temp"

rm -f "$temp"


echo ===== command redirection =====

cat <(echo 1)
cat <(echo 2)-
echo >(cat) 3 | cat

seq () {
	i=1
	while [ "$i" -le "$1" ]; do
		echo "$i"
		i=$((i+1))
	done
	unset i
}

seq 4 >(cat) | grep 4
grep 5 <(seq 5)
! $INVOKE $TESTEE -c '< (:)' 2>(cat>/dev/null)
echo $?


echo ===== here-string =====

var=foo
cat <<<123
cat <<< "$var"
cat <<< "-
-"

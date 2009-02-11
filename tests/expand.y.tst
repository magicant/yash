[ ~+ = $PWD ] && echo \~+
cd .
[ ~- = $OLDPWD ] && echo \~-

var=123/456/789 asterisks='*****'

echo expand.y.t${{asterisks#"**"}%"**"}st
echo ${#$(echo a)} ${${`echo abc`#a}%c}

echo ${var/456/xxx}
echo ${var/#123/xxx} ${var/#456/xxx}
echo ${var/%789/xxx} ${var/%456/xxx}
echo ${var//\//-} ${var//5//} ${var///-}
echo ${var:/123*789/x}

$INVOKE $TESTEE -c 'echo "${#*}"' x 1 22 333 4444 55555
$INVOKE $TESTEE -c 'echo "${#@}"' x 1 22 333 4444 55555

echo $((echo foo); (echo bar))

var=1-2-3
echo $var ${IFS= -} $var
unset IFS

echo ===== 1 =====

echo "${var[2,4]}"
echo "${var[3,3]}" "${var[3]}"
echo "${var[4,3]}"
(echo "${var[0]}") 2>/dev/null || echo zero index
echo "${var[@]}" "${var[*]}" ${var[#]}

echo ===== 2 =====

echol () {
	typeset i  # local
	for i; do printf "%s\n" "$i"; done
}

ary=(1 22 '3  3' 4"   "4 5)
echol "${ary}" -0-
echol "${ary[@]}" -1-
echol "${ary[*]}" -2-
echol "${ary[#]}" -3-
echol "${ary[1]}" -4-
echol "${ary[2]}" -5-
echol "${ary[3]}" -6-
echol "${ary[4]}" -7-
echol "${ary[5]}" -8-
echol "${ary[1,3]}" -9-
echol "${ary[2,4]}" -10-
echol "${ary[3,5]}" -11-
echol "${ary[4,6]}" -12-
echol "${ary[5,7]}" -13-
echol "${ary[6,8]}" -14-
echol "${${ary}}" -20-
echol "${${ary[3]}[1]}" -21-
echol "${${ary[2,4]}[2]}" -22-
echol ${ary} -30-
echol ${ary[@]} -31-

echo ===== 3 =====

set "$ary"
unset -v ary
echo "-$ary-"
echol "${@}" -0-
echol "${@[@]}" -1-
echol "${@[*]}" -2-
echol "${@[#]}" -3-
echol "${@[1]}" -4-
echol "${@[2]}" -5-
echol "${@[3]}" -6-
echol "${@[4]}" -7-
echol "${@[5]}" -8-
echol "${@[1,3]}" -9-
echol "${@[2,4]}" -10-
echol "${@[3,5]}" -11-
echol "${@[4,6]}" -12-
echol "${@[5,7]}" -13-
echol "${@[6,8]}" -14-
echol "${${@}}" -20-
echol "${${@[3]}[1]}" -21-
echol "${${@[2,4]}[2]}" -22-
echol ${@} -30-
echol ${@[@]} -31-

echo ===== 4 =====

echol "${*}" -0-
echol "${*[@]}" -1-
echol "${*[*]}" -2-
echol "${*[#]}" -3-
echol "${*[1]}" -4-
echol "${*[2]}" -5-
echol "${*[3]}" -6-
echol "${*[4]}" -7-
echol "${*[5]}" -8-
echol "${*[1,3]}" -9-
echol "${*[2,4]}" -10-
echol "${*[3,5]}" -11-
echol "${*[4,6]}" -12-
echol "${*[5,7]}" -13-
echol "${*[6,8]}" -14-
echol "${${*}}" -20-
echol "${${*[3]}[1]}" -21-
echol "${${*[2,4]}[2]}" -22-
echol ${*} -30-
echol ${*[@]} -31-

echo ===== 5 =====

echol "${@[1 + (4 / 2), 5 - 1]}"

[ ~+ = $PWD ] && echo \~+
cd .
[ ~- = $OLDPWD ] && echo \~-

var=123/456/789 asterisks='*****'

echo expand.y.t${{asterisks#"**"}%"**"}st
echo ${#$(echo a)} ${${`echo abc`#a}%c}
[ x"${$+x}" != x"${$:+y}" ] && echo nest \$ +
[ x"${$-x}" =  x"${$:-y}" ] && echo nest \$ -
[ x"${$=x}" =  x"${$:=y}" ] && echo nest \$ =
[ x"${$?x}" =  x"${$:?y}" ] && echo nest \$ ?
[ x"${$#x}" =  x"${$##y}" ] && echo nest \$ \#
[ x"${$%x}" =  x"${$%%y}" ] && echo nest \$ %
[ x"${$/x/y}" = x"${$:/x/y}" ] && echo nest \$ /

echo ${var/456/xxx}
echo ${var/#123/xxx} ${var/#456/xxx}
echo ${var/%789/xxx} ${var/%456/xxx}
echo ${var//\//-} ${var//5//} ${var///-}
echo ${var:/123*789/x}

$INVOKE $TESTEE -c 'echo "${#*}"' x 1 22 333 4444 55555
$INVOKE $TESTEE -c 'echo "${#@}"' x 1 22 333 4444 55555

echo $((echo foo); (echo bar))

var=1-2-3 IFS=
echo $var ${IFS:= -} $var
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

echo ===== 6 =====

ary=('' '' '')
printf '[%s]' "${ary[@]}"; echo
echo - ${ary[1]=1} ${ary[2]=2} ${ary[3]=3}
echo - ${ary[1]:=1}
printf '[%s]' "${ary[@]}"; echo
echo - ${ary[2]:=2}
echo - ${ary[1]:=X}
printf '[%s]' "${ary[@]}"; echo

readonly ary
( echo - ${ary[3]:=3} ) 2>/dev/null
[ $? -ne 0 ]; echo $?
printf '[%s]' "${ary[@]}"; echo

export ary
env | grep ^ary=

echo ===== 7 =====

set -o braceexpand
echo {1..9 {1.9} 1..9}
echo {1..9}
echo {9..11}
echo {111..123}
echo {9..1}
echo {11..9}
echo {123..111}
echo {1..5..2}
echo {1..19..3}
echo {1..2..3}
echo {1..1..1}
echo {0..0..0}
echo {5..1..-2}
echo {19..1..-3}
echo {1..1..-1}
echo {-11..-19..-2}
echo a{1..4}b
echo a{1,2,3,4,,555}b
echo a{1,2}b{3,4}c
echo a{b{1,2}c,3}d
echo a{b{1..2}c,3}d
echo {1,,2}
echo a{1\'\\\"2,3}b
echo \{1,2,3} {4,5,6\}
echo a{,}b

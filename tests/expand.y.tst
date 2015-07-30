# expand.y.tst: yash-specific test of word expansions
# vim: set ft=sh ts=8 sts=4 sw=4 noet:

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
set 1 22 333
echo \## length=${##}
echo \#- length=${#-} ifunset=${#-""}
echo \#? length=${#?} error=${#?error}
echo \#+ ${#+} ${#:+}
echo \#= ${#=} ${#:=}
echo \#/ ${#/3/X} ${#:/3/X}
echo \#% ${#%3}

echo ${var/456/xxx}
echo ${var/#123/xxx} ${var/#456/xxx}
echo ${var/%789/xxx} ${var/%456/xxx}
echo ${var//\//-} ${var//5//} ${var///-}
echo ${var:/123*789/x}

$INVOKE $TESTEE -c 'echo "${#*}"' x 1 22 333 4444 55555
$INVOKE $TESTEE -c 'echo "${#@}"' x 1 22 333 4444 55555
$INVOKE $TESTEE -c 'echo  ${#*} ' x 1 22 333 4444 55555
$INVOKE $TESTEE -c 'echo  ${#@} ' x 1 22 333 4444 55555

echo $((echo foo); (echo bar))

var=1-2-3 IFS=
echo $var ${IFS:= -} $var
unset IFS

echo ===== 1 =====

echo "${var[2,4]}"
echo "${var[3,3]}" "${var[3]}"
echo "${var[4,3]}"
echo "${var[0]}"
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

s=abcd
echo $s ${s[@]} ${s[*]} ${s[#]}
echo ${s[0]}   . ${s[1]}   ${s[2]}   ${s[3]}   ${s[4]}   . ${s[5]}
echo ${s[-0]}  . ${s[-1]}  ${s[-2]}  ${s[-3]}  ${s[-4]}  . ${s[-5]}
echo === a ===
echo ${s[0,0]} . ${s[0,1]} ${s[0,2]} ${s[0,3]} ${s[0,4]} . ${s[0,5]}
echo ${s[1,0]} . ${s[1,1]} ${s[1,2]} ${s[1,3]} ${s[1,4]} . ${s[1,5]}
echo ${s[2,0]} . ${s[2,1]} ${s[2,2]} ${s[2,3]} ${s[2,4]} . ${s[2,5]}
echo ${s[3,0]} . ${s[3,1]} ${s[3,2]} ${s[3,3]} ${s[3,4]} . ${s[3,5]}
echo ${s[4,0]} . ${s[4,1]} ${s[4,2]} ${s[4,3]} ${s[4,4]} . ${s[4,5]}
echo ${s[5,0]} . ${s[5,1]} ${s[5,2]} ${s[5,3]} ${s[5,4]} . ${s[5,5]}
echo ${s[-0,0]} . ${s[-0,1]} ${s[-0,2]} ${s[-0,3]} ${s[-0,4]} . ${s[-0,5]}
echo ${s[-1,0]} . ${s[-1,1]} ${s[-1,2]} ${s[-1,3]} ${s[-1,4]} . ${s[-1,5]}
echo ${s[-2,0]} . ${s[-2,1]} ${s[-2,2]} ${s[-2,3]} ${s[-2,4]} . ${s[-2,5]}
echo ${s[-3,0]} . ${s[-3,1]} ${s[-3,2]} ${s[-3,3]} ${s[-3,4]} . ${s[-3,5]}
echo ${s[-4,0]} . ${s[-4,1]} ${s[-4,2]} ${s[-4,3]} ${s[-4,4]} . ${s[-4,5]}
echo ${s[-5,0]} . ${s[-5,1]} ${s[-5,2]} ${s[-5,3]} ${s[-5,4]} . ${s[-5,5]}
echo === b ===
echo ${s[0,-0]} . ${s[0,-1]} ${s[0,-2]} ${s[0,-3]} ${s[0,-4]} . ${s[0,-5]}
echo ${s[1,-0]} . ${s[1,-1]} ${s[1,-2]} ${s[1,-3]} ${s[1,-4]} . ${s[1,-5]}
echo ${s[2,-0]} . ${s[2,-1]} ${s[2,-2]} ${s[2,-3]} ${s[2,-4]} . ${s[2,-5]}
echo ${s[3,-0]} . ${s[3,-1]} ${s[3,-2]} ${s[3,-3]} ${s[3,-4]} . ${s[3,-5]}
echo ${s[4,-0]} . ${s[4,-1]} ${s[4,-2]} ${s[4,-3]} ${s[4,-4]} . ${s[4,-5]}
echo ${s[5,-0]} . ${s[5,-1]} ${s[5,-2]} ${s[5,-3]} ${s[5,-4]} . ${s[5,-5]}
echo ${s[-0,-0]} . ${s[-0,-1]} ${s[-0,-2]} ${s[-0,-3]} ${s[-0,-4]} . ${s[-0,-5]}
echo ${s[-1,-0]} . ${s[-1,-1]} ${s[-1,-2]} ${s[-1,-3]} ${s[-1,-4]} . ${s[-1,-5]}
echo ${s[-2,-0]} . ${s[-2,-1]} ${s[-2,-2]} ${s[-2,-3]} ${s[-2,-4]} . ${s[-2,-5]}
echo ${s[-3,-0]} . ${s[-3,-1]} ${s[-3,-2]} ${s[-3,-3]} ${s[-3,-4]} . ${s[-3,-5]}
echo ${s[-4,-0]} . ${s[-4,-1]} ${s[-4,-2]} ${s[-4,-3]} ${s[-4,-4]} . ${s[-4,-5]}
echo ${s[-5,-0]} . ${s[-5,-1]} ${s[-5,-2]} ${s[-5,-3]} ${s[-5,-4]} . ${s[-5,-5]}

echo ===== 8 =====

a=(a b c d)
echo "$a" "${a[@]}" "${a[*]}" "${a[#]}"
echo ${a[0]}  . ${a[1]}  ${a[2]}  ${a[3]}  ${a[4]}  . ${a[5]}
echo ${a[-0]} . ${a[-1]} ${a[-2]} ${a[-3]} ${a[-4]} . ${a[-5]}
for i in 0 1 2 3 4 5 -0 -1 -2 -3 -4 -5; do
    for j in 0 1 2 3 4 5 -1 -2 -3 -4 -5 -0; do
	echo "\${a[$i,$j]} = ${a[$i,$j]}"
    done
done

echo ===== 9 =====

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

echo ===== 10 =====

x=1
echo pre/postfix $((x++)) $((++x)) $((x--)) $((--x)) $((x))

echo ===== 11 ===== >&2

$INVOKE $TESTEE -c ': $((0/0))'
$INVOKE $TESTEE -c ': $((1/0))'
$INVOKE $TESTEE -c ': $((0%0))'
$INVOKE $TESTEE -c ': $((1%0))'
$INVOKE $TESTEE -c 'x=0 && : $((x/=0))'
$INVOKE $TESTEE -c 'x=1 && : $((x/=0))'
$INVOKE $TESTEE -c 'x=0 && : $((x%=0))'
$INVOKE $TESTEE -c 'x=1 && : $((x%=0))'

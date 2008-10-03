RANDOM=123
(echo $RANDOM $RANDOM $RANDOM $RANDOM $RANDOM
RANDOM=456
echo $RANDOM $RANDOM $RANDOM $RANDOM $RANDOM) >"${TESTTMP}/variable-a"
(echo $RANDOM $RANDOM $RANDOM $RANDOM $RANDOM
RANDOM=456
echo $RANDOM $RANDOM $RANDOM $RANDOM $RANDOM) >"${TESTTMP}/variable-b"
diff "${TESTTMP}/variable-a" "${TESTTMP}/variable-b" && echo ok

oldrand=$RANDOM rand=$RANDOM
while [ "$oldrand" = "$rand" ]; do
	oldrand=$rand rand=$RANDOM
done

echo ===== 1 =====

func1 () { echo func1; }
func2 () { echo func2; }

func1; func2;

func1 () {
	func1 () { echo re-defined func1; }
	func2 () { echo re-defined func2; }
	echo re-defined.
}
func2; func1; func2; func1;

echo ===== 2 =====

echol () {
	typeset i  # local
	for i
	do printf "%s\n" "$i"
	done
}

unset i
ary=123
echol "$ary"
ary=(1 22 '3  3' 4\ \ \ 4 5)
echol "$ary"
ary=456
echol "$ary"
ary=(9\
9
8'
'8"
"8
7 #comment
${ary/5/ })
echol "$ary"
ary=(test of array)
echo i "${i-unset}"

ary0=()
unset ary0
echo ary0 "${ary0-unset}"

echo ===== typeset export =====

func () {
	typeset foo=abc bar
	bar=def
	echo $foo$bar
	export foo bar=xyz
	$INVOKE $TESTEE -c 'echo $foo$bar'
	typeset -g baz qux=global
}
foo=foo bar=bar func
$INVOKE $TESTEE -c 'echo $foo $bar'
echo ${baz-unset} $qux

func () {
	export -g baz qux
	export -p baz qux
}
func
$INVOKE $TESTEE -c 'echo ${baz-unset} $qux'

typeset -px baz qux

typeset -X qux
$INVOKE $TESTEE -c 'echo ${qux-unset}'
echo ${qux-unset}

export ary
export -p ary
env | grep "^ary="

echo ===== unset readonly =====

unset foo bar baz qux
$INVOKE $TESTEE -c 'echo ${foo-unset} ${bar-unset} ${baz-unset} ${qux-unset}'

readonly ro=readonly
echo $ro
func () {
	typeset ro=local
	echo func $ro
	unset ro
	echo func $ro
	typeset ro
	ro=localagain
	echo func $ro
	readonly -x ro2=export
	$INVOKE $TESTEE -c 'echo $ro2'
	(ro2=xxx; unset ro2; $INVOKE $TESTEE -c 'echo $ro2') 2>/dev/null
	readonly -p
}
func
unset ro3
ro3=dummy typeset -r ro3=ro3

typeset -pr ro ro3

for num in 1 2 3 4 5; do
	echo $num
	if [ $num = 3 ]; then
		readonly num
	fi
done 2>/dev/null

readonly ary
ary=(cannot assign) 2>/dev/null
readonly -p ary
typeset -p ary
echo $ary

echo ===== function typeset =====

func () {
	echo ok
}
typeset -fp func
typeset -fp

readonly -f func
{
	func () { echo invalid re-definition; }
} 2>/dev/null
func

echo ===== array =====

array aaa 1 2 3 4 5 6 7 8 9
array
array -d aaa 6 2 10 2 8
echo $aaa
array -d aaa -1 -5 -20 0
echo $aaa
array -i aaa 0 0
echo $aaa
array -i aaa 2 2 3
echo $aaa
array -i aaa 6 6
echo $aaa
array -i aaa -1 8
echo $aaa
array -i aaa 100 9
echo $aaa
array -i aaa -100 !
echo $aaa
array -s aaa 1 0
array -s aaa 2 1
array -s aaa 10 -
echo $aaa
array -s aaa 100 x 2>/dev/null ||
array -s aaa -100 x 2>/dev/null ||
echo $aaa

echo ===== read =====

unset a
printf "%s" '12\' | (
read a
echo $? "$a"
)
printf "%s" '12\' | (
read -r a
echo $? "$a"
)

echo =====

unset IFS a b c d e

read -A a b c <<END
1 2 33
END
echo "${a}" "${b}" "${c}" "${c[#]}" "${{c[1]}[#]}"

read --array a b c <<END
1 2 3 4  5
END
echo "${a}" "${b}" "${c}"
echo "${c[1]}"
echo "${c[2]}"
echo "${c[3]}"
echo "${c[#]}"

read -A a b c <<END
1 2 3
4 5
END
echo "${a}" "${b}" "${c}"

read --array a b c <<END
1 2 3\
4  5
6 7
END
echo "${a}"
echo "${b}"
echo "${c[1]}"
echo "${c[2]}"
echo "${c[#]}"

read -A a b c d e <<END
1 2 3
4 5
END
echo "${a}"
echo "${b}"
echo "${c}"
echo "${d}"
echo "${e[#]}"

echo =====

IFS=- read --array a <<END
1 2\-3-4 5\-6-7\-8-9
END
echo "${a[1]}"
echo "${a[2]}"
echo "${a[3]}"
echo "${a[4]}"
echo "${a[#]}"

IFS=- read -Ar a <<\END
1 2\-3-4 5\-6-7\-8-9\
0
END
echo "${a[1]}"
echo "${a[2]}"
echo "${a[3]}"
echo "${a[4]}"
echo "${a[5]}"
echo "${a[6]}"
echo "${a[7]}"
echo "${a[#]}"

while read a b -A
do
	echo "$b" ${b[#]}
done <<END
1 2 3 4
5 6 7 8
9 0 1
END


rm -f "${TESTTMP}/variable-a" "${TESTTMP}/variable-b"

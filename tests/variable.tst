RANDOM=123
(echo $RANDOM $RANDOM $RANDOM $RANDOM $RANDOM
RANDOM=456
echo $RANDOM $RANDOM $RANDOM $RANDOM $RANDOM) >"${TESTTMP}/variable-a"
(echo $RANDOM $RANDOM $RANDOM $RANDOM $RANDOM
RANDOM=456
echo $RANDOM $RANDOM $RANDOM $RANDOM $RANDOM) >"${TESTTMP}/variable-b"
diff "${TESTTMP}/variable-a" "${TESTTMP}/variable-b" && echo ok

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

typeset -pr ro

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


rm -f "${TESTTMP}/variable-a" "${TESTTMP}/variable-b"

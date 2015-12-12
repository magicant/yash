# variable.y.tst: yash-specific test of variables
# vim: set ft=sh ts=8 sts=4 sw=4 noet:

ary=(test of array)
export ary

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

echo ===== function typeset/unset =====

func () {
    echo ok $?
}
function x=1 {
    echo equal
}
{
    typeset -fp func
    typeset -fp x=1
    typeset -fp
} | sed 's/^ *//'

readonly -f func
{
    func () { echo invalid re-definition; }
} 2>/dev/null
func
unset -f func 2>/dev/null
func

unset -f x=1
typeset -fp x=1 2>/dev/null
func

func2() {
    echo $((echo func2) )
}
eval "$(typeset -fp func2)"
func2


rm -f "${TESTTMP}/variable.y.tm1" "${TESTTMP}/variable.y.tm2"

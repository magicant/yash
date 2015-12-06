# variable.y.tst: yash-specific test of variables
# vim: set ft=sh ts=8 sts=4 sw=4 noet:

RANDOM=123
(echo $RANDOM $RANDOM $RANDOM $RANDOM $RANDOM
RANDOM=456
echo $RANDOM $RANDOM $RANDOM $RANDOM $RANDOM) >"${TESTTMP}/variable.y.tm1"
(echo $RANDOM $RANDOM $RANDOM $RANDOM $RANDOM
RANDOM=456
echo $RANDOM $RANDOM $RANDOM $RANDOM $RANDOM) >"${TESTTMP}/variable.y.tm2"
diff "${TESTTMP}/variable.y.tm1" "${TESTTMP}/variable.y.tm2" && echo ok

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

echo ===== 3 =====

(
YASH_AFTER_CD='echo -\> "$PWD"'
cd /
YASH_AFTER_CD=('echo cd' 'continue -i; echo ng' 'break -i' 'echo NG')
cd "$OLDPWD"
)

echo ===== 4 =====

(
savepath=$PATH
COMMAND_NOT_FOUND_HANDLER='PATH=$savepath echo not found: "$@"' \
    PATH= _no_such_command_ a b c
echo exitstatus=$?
_no_such_command_ a b c
echo exitstatus=$?
COMMAND_NOT_FOUND_HANDLER='echo cd "$@" && cd "$@" && HANDLED=1'
/
echo exitstatus=$? HANDLED=${HANDLED:-unset} PWD=$PWD
"$OLDPWD" >/dev/null
echo exitstatus=$? HANDLED=${HANDLED:-unset}
) 2>/dev/null

COMMAND_NOT_FOUND_HANDLER=('echo not found: "$@"' \
'HANDLED=1' 'unset COMMAND_NOT_FOUND_HANDLER')
/dev /tmp   # 2>/dev/null
echo ${COMMAND_NOT_FOUND_HANDLER-unset}

# handler is not called in handler
COMMAND_NOT_FOUND_HANDLER='_no_such_command_' _no_such_command_ 2>/dev/null
echo exitstatus=$?

echo ===== typeset export =====

func () {
    typeset foo=abc bar
    bar=def
    echo $foo$bar
    export foo bar=xyz
    $INVOKE $TESTEE -c 'echo $foo$bar'
    typeset -g baz qux=global
    typeset
    typeset -g | grep '^typeset baz$'
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

(
ary=()
typeset -p ary
)

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

# builtin.y.tst: yash-specific test of builtins
# vim: set ft=sh ts=8 sts=4 sw=4 noet:

tmp="${TESTTMP}/test.y.tmp"
mkdir "$tmp"

returnfunc ()
while do
    return -n
    return
done

echo ===== eval =====

eval echo -i
eval -i -- 'echo 1' 'echo 2'
eval -i '(exit 2)' 'echo status1=$?'

eval 'echo 1; returnfunc; echo $?'
eval -i 'echo 2' 'returnfunc' 'echo $?'
#eval 'echo 3; return; echo not reached'
#eval -i 'echo 4' 'return; echo not reached' 'echo $?'

$INVOKE $TESTEE -i +m --norcfile 2>/dev/null <<\END
returnfunc ()
while do
    return -n
    return
done
evalreturnfunc() {
    echo eval return ok
    eval return
    echo eval return ng
}
evalireturnfunc() {
    eval -i 'echo eval -i return ok' return 'echo eval -i return ng'
}

eval 'echo 5; returnfunc; echo $?'
eval -i 'echo 6' 'returnfunc' 'echo $?'
eval 'echo 7; evalreturnfunc; echo 7-2'
eval -i 'echo 8' 'evalireturnfunc' 'echo $?'
END


echo ===== . =====

set a b c
. ./dot.t 1 2 3
echo $count
echo -"$@"-

# test of autoload
mkdir "$tmp/dir"
cat >"$tmp/script1" <<\EOF
echo script1
EOF
cat >"$tmp/dir/script1" <<\EOF
echo dir/script1
EOF
cat >"$tmp/dir/script2" <<\EOF
echo dir/script2
EOF
YASH_LOADPATH=("$tmp/dummy" "$tmp" "$tmp/dir" "$tmp/dummy")
. -L script1
. -L dir/script1
. --autoload script2


echo ===== command =====

command -V if then else elif fi do done case esac while until for function \
    { } ! in
PATH= command -V _no_such_command_ 3>&1 1>&2 2>&3 || echo $?
command -V : . break continue eval exec exit export readonly return set shift \
    times trap unset
#TODO command -V newgrp
command -V bg cd command false fg getopts jobs kill pwd read true umask wait

testreg() {
    command -V $1 | grep -v "^$1: a regular built-in "
}
testreg typeset
testreg disown
testreg type

command -Vb sh 2>&1 || PATH= command -vp sh >/dev/null && echo ok

command -b eval echo echo
echo command -b eval = $?
command --builtin-command cat /dev/null 2>/dev/null
echo command -b cat = $?
command -e ls >/dev/null
echo command -e ls = $?
PATH= command --external-command exit 50 2>/dev/null
echo command -e exit 50 = $?
(command exit 10; echo not reached)
echo command exit 10 = $?
command -f testreg type
echo command -f testreg = $?
command --function echo 2>/dev/null
echo command -f echo = $?

type type | grep -v "^type: a regular built-in "

command -vb cat
echo command -vb cat = $?
PATH= command -ve exit
echo command -ve exit = $?

(
cd() { command cd "$@"; }
if command -vb alias >/dev/null 2>&1; then
    alias cd=cd_alias
    type cd
    command -va cd
    command -va echo || echo $?
else
    echo "cd: an alias for \`cd_alias'"
    echo "alias cd='cd_alias'"
    echo 1
fi
echo =1=
type -b cd
echo =2=
type -k if
type -k cd 2>&1
)

function slash/function () {
    echo slash "$@"
    return 7
}
command -f slash/function a b "C   C"
echo $?
unset -f slash/function
command -bef slash/function 2>/dev/null
echo $?


rm -R "$tmp"


echo ===== exec =====

(exec -a sh $TESTEE -c 'echo $0')
(exec --as=sh $TESTEE -c 'echo $0')
(foo=123 bar=456 baz=(7 8 9) exec -c env | grep -v ^_ | sort)
(foo=123 bar=456 baz=(7 8 9) exec --clear env | grep -v ^_ | sort)

# command-y.tst: yash-specific test of the command and type built-ins

# Seemingly meaningless comments like #` in this script are to work around
# syntax highlighting errors on some editors.

test_oE -e 0 'executing with -b option'
command -b eval echo foo
__IN__
foo
__OUT__

test_Oe -e 127 'external command is not found with -b option'
command -b cat /dev/null
__IN__
command: no such command `cat'
__ERR__
#`

test_OE -e 0 'executing with -e option'
command -e cat /dev/null
__IN__

test_Oe -e 127 'built-in command is not found with -e option'
PATH=
command -e exit 10
__IN__
command: no such command `exit'
__ERR__
#`

test_oE -e 0 'executing with -f option'
exit() { echo foo; }
command -f exit 1
__IN__
foo
__OUT__

test_oE -e 0 'executing function with name containing slash'
function foo/bar {
    echo "$@"
}
command -f foo/bar baz 'x  x'
__IN__
baz x  x
__OUT__

test_Oe -e 127 'external command is not found with -f option'
command -f cat /dev/null
__IN__
command: no such command `cat'
__ERR__
#`

test_oE -e 0 'describing alias (-V)'
alias a='foo'
command -V a
__IN__
a: an alias for `foo'
__OUT__
#`

test_oE -e 0 'describing special built-ins (-V)'
command -V : . break continue eval exec exit export readonly return set shift \
    times trap unset
__IN__
:: a special built-in
.: a special built-in
break: a special built-in
continue: a special built-in
eval: a special built-in
exec: a special built-in
exit: a special built-in
export: a special built-in
readonly: a special built-in
return: a special built-in
set: a special built-in
shift: a special built-in
times: a special built-in
trap: a special built-in
unset: a special built-in
__OUT__

# `newgrp' is not a semi-special built-in in yash.
test_oE -e 0 'describing semi-special built-ins (-V)'
command -V bg cd command false fg getopts hash jobs kill pwd read true type \
    umask wait
__IN__
bg: a semi-special built-in
cd: a semi-special built-in
command: a semi-special built-in
false: a semi-special built-in
fg: a semi-special built-in
getopts: a semi-special built-in
hash: a semi-special built-in
jobs: a semi-special built-in
kill: a semi-special built-in
pwd: a semi-special built-in
read: a semi-special built-in
true: a semi-special built-in
type: a semi-special built-in
umask: a semi-special built-in
wait: a semi-special built-in
__OUT__

(
if ! testee -c 'command -bv ulimit' >/dev/null; then
    skip="true"
fi

test_oE -e 0 'describing semi-special built-in ulimit (-V)'
command -V ulimit
__IN__
ulimit: a semi-special built-in
__OUT__

)

(
if ! testee -c 'command -bv echo' >/dev/null; then
    skip="true"
fi

test_OE 'describing regular built-in (-V)'
command -V echo | grep -v "^echo: a regular built-in "
__IN__
)

test_OE -e 0 'describing external command (-V)'
command -V cat | grep -q '^cat: an external command at'
__IN__

test_oE -e 0 'describing function (-V)'
true() { :; }
type -V true
__IN__
true: a function
__OUT__

test_oE -e 0 'describing reserved words (-V)'
command -V if then else elif fi do done case esac while until for function \
    { } ! in
__IN__
if: a shell keyword
then: a shell keyword
else: a shell keyword
elif: a shell keyword
fi: a shell keyword
do: a shell keyword
done: a shell keyword
case: a shell keyword
esac: a shell keyword
while: a shell keyword
until: a shell keyword
for: a shell keyword
function: a shell keyword
{: a shell keyword
}: a shell keyword
!: a shell keyword
in: a shell keyword
__OUT__

test_oE -e 0 'describing alias with -a option'
alias a='foo'
command -va a &&
command --identify --alias a
__IN__
alias a=foo
alias a=foo
__OUT__

test_oE -e 0 'describing built-ins with -b option'
command -vb : bg &&
command --identify --builtin-command : bg
__IN__
:
bg
:
bg
__OUT__

test_E -e 0 'describing external command with -e option'
command -ve cat &&
command --identify --external-command cat
__IN__

(
cd -P . # normalize $PWD
case $PWD in (//*|*/) skip="true"; esac

>foo
chmod a+x foo

testcase "$LINENO" \
    -e 0 'output of describing absolute external command (-v, with slash)' \
    3<<\__IN__ 4<<__OUT__ 5</dev/null
command -v "${PWD}/foo"
__IN__
${PWD}/foo
__OUT__

testcase "$LINENO" \
    -e 0 'output of describing relative external command (-v, with slash)' -e \
    3<<\__IN__ 4<<__OUT__ 5</dev/null
command -v "./foo"
cd /
command -v "${OLDPWD#/}/foo"
__IN__
${PWD}/./foo
${PWD}/foo
__OUT__

)

test_oE -e 0 'describing function with -f option'
true() { :; }
command -vf true &&
command --identify --function true
__IN__
true
true
__OUT__

test_oE -e 0 'describing reserved word with -k option'
command -vk if &&
command --identify --keyword if
__IN__
if
if
__OUT__

test_OE -e 1 'describing non-existent command (-va)'
command -va exit
__IN__

test_OE -e 1 'describing non-existent command (-vb)'
command -vb cat
__IN__

test_OE -e 1 'describing non-existent command (-ve)'
PATH=
command -ve exit
__IN__

test_OE -e 1 'describing non-existent command (-vk)'
command -vk exit
__IN__

test_OE -e 1 'describing non-existent command (-vf)'
command -vf exit
__IN__

test_Oe -e 1 'describing non-existent command (-V)'
PATH=
command -V _no_such_command_
__IN__
command: no such command `_no_such_command_'
__ERR__
#`

test_oE -e 0 'describing with long option'
command --verbose-identify if : bg
__IN__
if: a shell keyword
:: a special built-in
bg: a semi-special built-in
__OUT__

test_oE -e 0 'describing with type command'
type if : bg
__IN__
if: a shell keyword
:: a special built-in
bg: a semi-special built-in
__OUT__

test_O -d -e 1 'printing to closed stream'
command -v command >&-
__IN__

test_Oe -e n 'using -a without -v'
command -a :
__IN__
command: the -a or -k option must be used with the -v option
__ERR__

test_Oe -e n 'using -k without -v'
command -k :
__IN__
command: the -a or -k option must be used with the -v option
__ERR__

test_Oe -e n 'invalid option'
command --no-such-option
__IN__
command: `--no-such-option' is not a valid option
__ERR__
#`

test_OE -e 0 'missing operand (non-POSIX)'
command
__IN__

(
posix="true"

test_oe 'argument syntax error in special built-in does not kill shell'
command . # missing operand
echo reached
__IN__
reached
__OUT__
.: this command requires an operand
__ERR__

test_Oe -e n 'missing operand (w/o -v, POSIX)'
command
__IN__
command: this command requires an operand
__ERR__

test_Oe -e n 'missing operand (with -v, POSIX)'
command -v
__IN__
command: this command requires an operand
__ERR__

test_Oe -e n 'missing operand (with -V, POSIX)'
command -V
__IN__
command: this command requires an operand
__ERR__

test_Oe -e n 'missing operand (type, POSIX)'
type
__IN__
type: this command requires an operand
__ERR__

test_Oe -e n 'more than one operand (with -v, POSIX)'
command -v foo bar
__IN__
command: too many operands are specified
__ERR__

test_Oe -e n 'more than one operand (with -V, POSIX)'
command -V foo bar
__IN__
command: too many operands are specified
__ERR__

)

# vim: set ft=sh ts=8 sts=4 sw=4 noet:

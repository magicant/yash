# option-y.tst: yash-specific test of shell options

# Tests related to globbing are in path-y.tst.

test_x -e 0 'hashondef (long) on: $-' -o hashondef
printf '%s\n' "$-" | grep -q h
__IN__

test_x -e 0 'hashondef (long) off: $-' +o hashondef
printf '%s\n' "$-" | grep -qv h
__IN__

test_o 'noexec is linewise'
set -n; echo executed
echo not executed
__IN__
executed
__OUT__

test_o 'noexec is ineffective when interactive' -in +m --norcfile
echo printed; exit; echo not printed
__IN__
printed
__OUT__

test_OE -e 0 'pipefail on: single command successful pipe' --pipefail
true
__IN__

test_OE -e 13 'pipefail on: single command unsuccessful pipe' --pipefail
(exit 13)
__IN__

test_OE -e 0 'pipefail on: multi-command successful pipe' --pipefail
true | true | true | true
__IN__

test_OE -e 7 'pipefail on: multi-command unsuccessful pipe' --pipefail
true | exit 2 | true | exit 7 | true | true
__IN__

test_OE -e 7 'pipefail on: multi-command unsuccessful pipe in subshell' \
    --pipefail
(true | exit 2 | true | exit 7 | true | true)
__IN__

test_oE 'traceall on: effect' --traceall
exec 2>&1
COMMAND_NOT_FOUND_HANDLER='echo not found $* >&2; HANDLED=1'
set -xv
no/such/command
__IN__
no/such/command
+ no/such/command
+ echo not found no/such/command
not found no/such/command
+ HANDLED=1
__OUT__

test_oE 'traceall on: effect' --notraceall
exec 2>&1
COMMAND_NOT_FOUND_HANDLER='echo not found $* >&2; HANDLED=1'
set -xv
no/such/command
__IN__
no/such/command
+ no/such/command
not found no/such/command
__OUT__

test_Oe -e 2 'unset off: unset variable $((foo))' -u
eval '$((x))'
__IN__
eval: arithmetic: parameter `x' is not set
__ERR__
#'
#`

test_x -e 0 'abbreviation of -o argument' -o allex
echo $- | grep -q a
__IN__

test_x -e 0 'abbreviation of +o argument' -a +o allexport
echo $- | grep -qv a
__IN__

test_x -e 0 'concatenation of -o and argument' -oallexport
echo $- | grep -q a
__IN__

test_x -e 0 'concatenation of option and -o' -ao errexit
echo $- | grep a | grep -q e
__IN__

test_x -e 0 'concatenation of option and -o and argument' -aoerrexit
echo $- | grep a | grep -q e
__IN__

# vim: set ft=sh ts=8 sts=4 sw=4 noet:

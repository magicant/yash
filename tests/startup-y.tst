# startup-y.tst: yash-specific test of shell startup

test_O -e 17 'one operand with -c' -c 'exit 17'
__IN__

test_o -e 0 'two operands with -c' \
    -c 'printf "[%s]\n" "$0" "$@"' 'command  name'
__IN__
[command  name]
__OUT__

test_o -e 0 'one positional parameter with -c' \
    -c 'printf "[%s]\n" "$0" "$@"' 0 1
__IN__
[0]
[1]
__OUT__

test_o -e 0 'many positional parameters with -c' \
    -c 'printf "[%s]\n" "$0" "$@"' 0 1 '2  2' 3 4 - 6 7 8 9 10 11
__IN__
[0]
[1]
[2  2]
[3]
[4]
[-]
[6]
[7]
[8]
[9]
[10]
[11]
__OUT__

test_oE -e 0 'stdin is not used with -c'
"$TESTEE" -c 'echo foo' <&-
__IN__
foo
__OUT__

test_oE -e 19 'no operands with -s' -s
echo $#
exit 19
__IN__
0
__OUT__

test_oE -e 23 'one operand with -s' -s '1  1'
printf "[%s]\n" "$@"
exit 23
__IN__
[1  1]
__OUT__

test_oE 'two operands with -s' -s '1  1' 2
printf "[%s]\n" "$@"
__IN__
[1  1]
[2]
__OUT__

test_oE 'many operands with -s' -s '1  1' 2 3 4 - 6 7 8 9 10 11
printf "[%s]\n" "$@"
__IN__
[1  1]
[2]
[3]
[4]
[-]
[6]
[7]
[8]
[9]
[10]
[11]
__OUT__

test_oE '$0 with -s'
exec -a '0  0' "$TESTEE" -s <<\__END__
printf '[%s]\n' "$0"
__END__
__IN__
[0  0]
__OUT__

test_oE -e 0 'negating -c and enabling -s' -c +c -s
echo ok
__IN__
ok
__OUT__

test_oE -e 0 'negating -s and enabling -c' -s +s -c 'echo ok'
__IN__
ok
__OUT__

testcase "$LINENO" -e 2 'missing command with -c' -c \
	3</dev/null 4</dev/null 5<<__ERR__
$testee: the -c option is specified but no command is given
__ERR__

testcase "$LINENO" -e 2 'options -c and -s are mutually exclusive (separate)' \
    -c -s 'echo XXX' 3</dev/null 4</dev/null 5<<__ERR__
$testee: the -c option cannot be used with the -s option
__ERR__

testcase "$LINENO" -e 2 'options -c and -s are mutually exclusive (combined)' \
    -cs 'echo XXX' 3</dev/null 4</dev/null 5<<__ERR__
$testee: the -c option cannot be used with the -s option
__ERR__

testcase "$LINENO" -e 2 'options -c and -s are mutually exclusive (long)' \
    --cmdlin --stdi 3</dev/null 4</dev/null 5<<__ERR__
$testee: the -c option cannot be used with the -s option
__ERR__

testcase "$LINENO" -e 2 'options -c and -s are mutually exclusive (-o)' \
    -o cmdlin -o stdi 3</dev/null 4</dev/null 5<<__ERR__
$testee: the -c option cannot be used with the -s option
__ERR__

(
input=./input$LINENO
cat >"$input" <<\__END__
echo input "$*"
cat
exit 3
echo not reached
__END__

test_oE -e 3 'reading file w/o positional parameters' "$input"
stdin
__IN__
input 
stdin
__OUT__

test_oE -e 3 'reading file with one positional parameter' "$input" '1  1'
stdin
__IN__
input 1  1
stdin
__OUT__

test_oE -e 3 'reading file with many positional parameters' \
    "$input" '1  1' 2 3 4 - 6 7 8 9 10 11
stdin
__IN__
input 1  1 2 3 4 - 6 7 8 9 10 11
stdin
__OUT__

)

(
input=input$LINENO
>"$input"
chmod a-r "$input"

# Skip if we're root.
if { <"$input"; } 2>/dev/null; then
    skip="true"
fi

test_O -d -e n 'reading non-readable file' "$input"
__IN__

)

test_O -d -e 127 'reading non-existing file' ./_no_such_file_
__IN__

(
unset YASH_LOADPATH

test_o 'LOADPATH is set to default if missing'
echo ${YASH_LOADPATH:+set}
__IN__
set
__OUT__

)

(
export YASH_LOADPATH=/foo/bar:/baz

test_o 'LOADPATH is not modified if exists in environment'
echo ${YASH_LOADPATH:-unset}
__IN__
/foo/bar:/baz
__OUT__

)

(
export HOME="${PWD%/}/home$LINENO"
mkdir "$HOME"
echo echo profile >"$HOME/.yash_profile"
echo echo yashrc >"$HOME/.yashrc"

test_oE 'startup: no argument'
echo $-
__IN__
s
__OUT__

test_oE 'startup: -c' -c 'echo $-'
__IN__
c
__OUT__

test_oE 'startup: -cl, short option, with profile' -cl 'echo $-'
__IN__
profile
cl
__OUT__

test_oE 'startup: -cl, long option, with profile' --cmdline --log-in 'echo $-'
__IN__
profile
cl
__OUT__

test_oE 'startup: -ci +m, short option, with rcfile' -ci +m 'echo $-'
__IN__
yashrc
ci
__OUT__

test_oE 'startup: -ci +m, long option, with rcfile' \
    --cmdline --interactive --no-monitor 'echo $-'
__IN__
yashrc
ci
__OUT__

test_oE 'startup: -cil +m, short option, with profile/rcfile' -cil +m 'echo $-'
__IN__
profile
yashrc
cil
__OUT__

test_oE 'startup: -cil +m, long option, with profile/rcfile' \
    --cmdline --interactive --log-in --no-monitor 'echo $-'
__IN__
profile
yashrc
cil
__OUT__

test_oE 'startup: -cil +m --noprofile' -cil +m --noprofile 'echo $-'
__IN__
yashrc
cil
__OUT__

test_oE 'startup: -cil +m --norcfile' -cil +m --norcfile 'echo $-'
__IN__
profile
cil
__OUT__

)

test_oE 'startup: -cl with unset HOME' -cl 'echo $-'
__IN__
cl
__OUT__

test_oE 'startup: -ci +m with unset HOME' -ci +m 'echo $-'
__IN__
ci
__OUT__

(
export HOME="${PWD%/}/_no_such_directory_"

test_oE 'startup: -cl with non-existing HOME' -cl 'echo $-'
__IN__
cl
__OUT__

test_oE 'startup: -ci +m with non-existing HOME' -ci +m 'echo $-'
__IN__
ci
__OUT__

)

(
profile="profile$LINENO"
rcfile="rcfile$LINENO"
echo echo local profile >"$profile"
echo echo local rcfile >"$rcfile"

test_oE 'startup: -cl, specified profile' -cl --profile="$profile" 'echo $-'
__IN__
local profile
cl
__OUT__

test_oE 'startup: -ci +m, specified rcfile' -ci +m --rcfile="$rcfile" 'echo $-'
__IN__
local rcfile
ci
__OUT__

test_oE 'startup: -cil +m, specified rcfile' \
    -cil +m --profile="$profile" --rcfile="$rcfile" 'echo $-'
__IN__
local profile
local rcfile
cil
__OUT__

)

(
# Ensure $PWD is safe to assign to $YASH_LOADPATH
case $PWD in (*[:%]*)
    skip="true"
esac

export HOME="${PWD%/}/home$LINENO"
export YASH_LOADPATH="$HOME/loadpath"
export ENV='${PWD%/}/_no_such_file_'
mkdir -p "$HOME/loadpath/initialization"
echo echo default >"$HOME/loadpath/initialization/default"

test_oE 'startup: -ci +m, LOADPATH fallback for missing yashrc' \
    -ci +m 'echo $-'
__IN__
default
ci
__OUT__

test_oE 'startup: -ci +m, no LOADPATH fallback in POSIX mode' \
    --posix -ci +m 'echo $-'
__IN__
ci
__OUT__

test_oE 'startup: -ci +m, no LOADPATH fallback with specified rcfile' \
    -ci +m --rcfile=_no_such_file_ 'echo $-'
__IN__
ci
__OUT__

echo echo yashrc >"$HOME/.yashrc"

test_oE 'startup: -ci +m, no LOADPATH fallback if ~/.yashrc found' \
    -ci +m 'echo $-'
__IN__
yashrc
ci
__OUT__

)

(
export HOME="${PWD%/}/home$LINENO"
mkdir "$HOME"
cat >"$HOME/.yash_profile" <<\__END__
echo error 1
. "$HOME/profile2"
echo error 1 syntax error \$\?=$?
unset var
echo ${var?}
echo error 1 expansion error \$\?=$?
fi
echo not reached
__END__
cat >"$HOME/profile2" <<\__END__
echo error 2
unset var
echo ${var?}
echo error 2 expansion error \$\?=$?
fi
echo not reached
__END__
ln -s .yash_profile "$HOME/.yashrc"

test_o -d -e 0 'errors in profile' -cl 'echo $-'
__IN__
error 1
error 2
error 2 expansion error $?=2
error 1 syntax error $?=258
error 1 expansion error $?=2
cl
__OUT__

test_o -d -e 0 'errors in rcfile' -ci +m 'echo $-'
__IN__
error 1
error 2
error 2 expansion error $?=2
error 1 syntax error $?=258
error 1 expansion error $?=2
ci
__OUT__

)

test_o 'startup: -abCcefhluvx' -abCcefhluvx 'echo $-'
__IN__
aCcefhlbuvx
__OUT__

test_o 'startup: -abCefhlsuvx' -abCefhlsuvx
echo $-
__IN__
aCefhlbsuvx
__OUT__

test_oE 'first operand is ignored if it is a hyphen (-c)' -c - 'echo $-'
__IN__
c
__OUT__

test_oE 'first operand is ignored if it is a hyphen (-s)' -s - -- 2
echo $- "$2" "$1"
__IN__
s 2 --
__OUT__

test_oE 'first operand is ignored if it is a hyphen (no -c or -s)' -
echo $- $#
__IN__
s 0
__OUT__

(
echo echo env >env
export ENV='${PWD%/}/env'

test_oE 'startup: --posix -c' --posix -c 'echo $-'
__IN__
c
__OUT__

test_oE 'startup: --posix -ci +m' --posix -ci +m 'echo $-'
__IN__
env
ci
__OUT__

)

test_oE 'startup: --posix -ci +m with unset ENV' --posix -ci +m 'echo $-'
__IN__
ci
__OUT__

(
export ENV='${PWD%/}/_no_such_file_'

test_oE 'startup: --posix -ci +m with non-existing ENV' --posix -ci +m 'echo $-'
__IN__
ci
__OUT__

)

test_oE 'program name yash disables POSIX mode (w/o directory name)'
exec -a yash "$TESTEE" <<\__END__
set +o | grep posixlycorrect
__END__
__IN__
set +o posixlycorrect
__OUT__

test_oE 'program name yash disables POSIX mode (with directory name)'
exec -a /bin/yash "$TESTEE" <<\__END__
set +o | grep posixlycorrect
__END__
__IN__
set +o posixlycorrect
__OUT__

test_oE 'program name sh enables POSIX mode (w/o directory name)'
exec -a sh "$TESTEE" <<\__END__
set +o | grep posixlycorrect
__END__
__IN__
set -o posixlycorrect
__OUT__

test_oE 'program name sh enables POSIX mode (with directory name)'
exec -a /bin/sh "$TESTEE" <<\__END__
set +o | grep posixlycorrect
__END__
__IN__
set -o posixlycorrect
__OUT__

test_oE 'hyphen prefix enables interactive mode (w/o directory name)'
exec -a -yash "$TESTEE" <<\__END__
echo $-
__END__
__IN__
ls
__OUT__

test_oE 'hyphen prefix enables interactive mode (with directory name)'
exec -a -/bin/yash "$TESTEE" <<\__END__
echo $-
__END__
__IN__
ls
__OUT__

# We cannot test this without mocking the terminal.
#test_oE 'interactive mode is enabled if stdin/stdout are terminal'

# Tested in job-y.tst
#test_oE 'job control is on by default in interactive shell'

(
if ! testee --version --verbose | grep -Fqx ' * help' ||
    ! testee --version --verbose | grep -Fqx ' * lineedit'; then
    skip="true"
fi

test_oE -e 0 'help' --help
__IN__
Syntax:
	yash [option...] [filename [argument...]]
	yash [option...] -c command [command_name [argument...]]
	yash [option...] -s [argument...]

Options:
	         --help
	-V       --version
	         --noprofile
	         --norcfile
	         --profile=...
	         --rcfile=...
	-a       -o allexport
	         -o braceexpand
	         -o caseglob
	+C       -o clobber
	-c       -o cmdline
	         -o curasync
	         -o curbg
	         -o curstop
	         -o dotglob
	         -o emacs
	         -o emptylastfield
	-e       -o errexit
	         -o errreturn
	+n       -o exec
	         -o extendedglob
	         -o forlocal
	+f       -o glob
	-h       -o hashondef
	         -o histspace
	         -o ignoreeof
	-i       -o interactive
	         -o lealwaysrp
	         -o lecompdebug
	         -o leconvmeta
	         -o lenoconvmeta
	         -o lepredict
	         -o lepredictempty
	         -o lepromptsp
	         -o levisiblebell
	         -o log
	-l       -o login
	         -o markdirs
	-m       -o monitor
	-b       -o notify
	         -o notifyle
	         -o nullglob
	         -o pipefail
	         -o posixlycorrect
	-s       -o stdin
	         -o traceall
	+u       -o unset
	-v       -o verbose
	         -o vi
	-x       -o xtrace

Try `man yash' for details.
__OUT__
#`

# No long options in the POSIXly-correct mode
test_oE -e 0 'help (POSIX)' --help --posixly-correct
__IN__
Syntax:
	sh [option...] [filename [argument...]]
	sh [option...] -c command [command_name [argument...]]
	sh [option...] -s [argument...]

Options:
	-a       -o allexport
	         -o braceexpand
	         -o caseglob
	+C       -o clobber
	-c       -o cmdline
	         -o curasync
	         -o curbg
	         -o curstop
	         -o dotglob
	         -o emacs
	         -o emptylastfield
	-e       -o errexit
	         -o errreturn
	+n       -o exec
	         -o extendedglob
	         -o forlocal
	+f       -o glob
	-h       -o hashondef
	         -o histspace
	         -o ignoreeof
	-i       -o interactive
	         -o lealwaysrp
	         -o lecompdebug
	         -o leconvmeta
	         -o lenoconvmeta
	         -o lepredict
	         -o lepredictempty
	         -o lepromptsp
	         -o levisiblebell
	         -o log
	-l       -o login
	         -o markdirs
	-m       -o monitor
	-b       -o notify
	         -o notifyle
	         -o nullglob
	         -o pipefail
	         -o posixlycorrect
	-s       -o stdin
	         -o traceall
	+u       -o unset
	-v       -o verbose
	         -o vi
	-x       -o xtrace

Try `man yash' for details.
__OUT__
#`

)

test_E -e 0 'version' --version
__IN__

test_E -e 0 'verbose version, short option' -Vv
__IN__

test_E -e 0 'verbose version, long option' --version --verbose
__IN__

testcase "$LINENO" -e 2 'version (short option in POSIX mode)' --posix -V \
	3</dev/null 4</dev/null 5<<__ERR__
$testee: \`V' is not a valid option
__ERR__
#'

testcase "$LINENO" -e 2 'version (long option in POSIX mode)' --posix --versi \
	3</dev/null 4</dev/null 5<<__ERR__
$testee: \`--versi' is not a valid option
__ERR__
#'

testcase "$LINENO" -e 2 'unexpected option argument' --norc=_unexpected_ \
	3</dev/null 4</dev/null 5<<__ERR__
$testee: --norc=_unexpected_: the --norcfile option does not take an argument
__ERR__

testcase "$LINENO" -e 2 'missing profile option argument' --profile \
	3</dev/null 4</dev/null 5<<__ERR__
$testee: the --profile option requires an argument
__ERR__

testcase "$LINENO" -e 2 'missing rcfile option argument' --rcfile \
	3</dev/null 4</dev/null 5<<__ERR__
$testee: the --rcfile option requires an argument
__ERR__

testcase "$LINENO" -e 2 'long option in POSIX mode' --posix --monitor \
	3</dev/null 4</dev/null 5<<__ERR__
$testee: \`--monitor' is not a valid option
__ERR__
#'

test_O -d -e 2 'ambiguous option' --p
__IN__

# vim: set ft=sh ts=8 sts=4 sw=4 noet:

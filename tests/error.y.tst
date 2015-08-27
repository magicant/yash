# error.y.tst: yash-specific test of error handling
# vim: set ft=sh ts=8 sts=4 sw=4 noet:

# error tests for alias/unalias are in alias.y.tst
# error tests for array are in array.y.tst
# error tests for help are in help.y.tst
# error tests for fg/bg are in job.y.tst
# error tests for fc/history are in history.y.tst
# error tests for pushd/popd/dirs are in dirstack.y.tst

echo ===== set =====
echo ===== set ===== >&2

set -c
echo set -c $?
set -i
echo set -i $?
set -l
echo set -l $?
set -s
echo set -s $?
set -V
echo set -V $?
set --cmdl
echo set --cmdline $?
set --interactive
echo set --interactive $?
set -o log-in
echo set --login $?
set --stdin
echo set --stdin $?
set --version
echo set --version $?
set --no-such-option
echo set no-such-option 1 $?
set -o no-such-option
echo set no-such-option 2 $?
(set >&- 2>/dev/null)
echo set output error $?
set --cu 2>/dev/null
echo set ambiguous option 1 $?
set --noh 2>/dev/null
echo set ambiguous option 2 $?
set -o nolog +o nolog -o
echo set missing argument $?
set -C-
echo set invalid option 1 $?
set -aXb
echo set invalid option 2 $?
$INVOKE $TESTEE --version=X
echo set unexpected option argument $?

echo ===== cd =====
echo ===== cd ===== >&2

cd --no-such-option
echo cd no-such-option $?
cd / /
echo cd too-many-operands $?
cd ./no/such/dir 2>/dev/null
echo cd no-such-dir $?
(
cd /
(unset OLDPWD; cd -)
echo cd no OLDPWD $?
(unset HOME; cd)
echo cd no HOME $?
(cd - >&- 2>/dev/null)
echo cd output error $?
)

echo ===== pwd =====
echo ===== pwd ===== >&2

pwd --no-such-option
echo pwd no-such-option $?
pwd foo
echo pwd invalid operand $?
(pwd >&- 2>/dev/null)
echo pwd output error $?

echo ===== hash =====
echo ===== hash ===== >&2

hash --no-such-option
echo hash no-such-option $?
hash -a operand
echo hash invalid operand $?
hash ./no/such/command
echo hash slash argument $?
(PATH=; hash no_such_command)
echo hash command not found $?
hash ls
(hash >&- 2>/dev/null)
echo hash output error $?

echo ===== umask =====
echo ===== umask ===== >&2

umask --no-such-option
echo umask no-such-option $?
umask too-many operands
echo umask too-many-operands $?
(umask >&- 2>/dev/null)
echo umask output error $?

echo ===== typeset =====
echo ===== typeset ===== >&2

typeset --no-such-option
echo typeset no-such-option $?
typeset -f -x foo
echo typeset invalid-option-combination 1 $?
typeset -f -X foo
echo typeset invalid-option-combination 2 $?
typeset -x -X foo
echo typeset invalid-option-combination 3 $?
(typeset >&- 2>/dev/null)
echo typeset output error 1 $?
(typeset -p >&- 2>/dev/null)
echo typeset output error 2 $?
(typeset -p PWD >&- 2>/dev/null)
echo typeset output error 3 $?
func() { :; }
(typeset -f >&- 2>/dev/null)
echo typeset output error 4 $?
(typeset -fp >&- 2>/dev/null)
echo typeset output error 5 $?
(typeset -fp func >&- 2>/dev/null)
echo typeset output error 6 $?
typeset -r readonly=readonly
typeset -r readonly=readonly
echo typeset readonly $?

echo ===== unset =====
echo ===== unset ===== >&2

unset --no-such-option
echo unset no-such-option $?
readonly -f func
unset readonly
echo unset readonly variable $?
unset -f func
echo unset readonly function $?

echo ===== shift =====
echo ===== shift ===== >&2

shift --no-such-option
echo shift no-such-option $?
shift 10000
echo shift shifting too many $?

echo ===== getopts =====
echo ===== getopts ===== >&2

getopts --no-such-option
echo getopts no-such-option $?
getopts
echo getopts operands missing 1 $?
getopts a
echo getopts operands missing 2 $?
getopts a:: var
echo getopts invalid opt $?
getopts abc var=iable
echo getopts invalid var $?

echo ===== trap =====
echo ===== trap ===== >&2

trap --no-such-option
echo trap no-such-option $?
trap foo
echo trap operands missing $?
trap '' KILL
echo trap KILL $?
trap '' STOP
echo trap STOP $?
(trap 'echo trap' INT TERM; trap >&- 2>/dev/null)
echo trap output error 1 $?
(trap 'echo trap' INT TERM; trap -p >&- 2>/dev/null)
echo trap output error 2 $?
(trap 'echo trap' INT TERM; trap -p INT >&- 2>/dev/null)
echo trap output error 3 $?
trap '' NO-SUCH-SIGNAL
echo trap no-such-signal $?

echo ===== kill =====
echo ===== kill ===== >&2

kill --no-such-option
echo kill no-such-option $?
kill -l -n 0
echo kill invalid-option-combination l n $?
kill -l -s INT
echo kill invalid-option-combination l s $?
kill
echo kill operands missing $?
kill -l 0
echo kill no-such-signal $?
kill %100
echo kill no-such-job $?
(kill -l >&- 2>/dev/null)
echo kill output error 1 $?
(kill -l HUP >&- 2>/dev/null)
echo kill output error 2 $?

echo ===== jobs kill =====
echo ===== jobs kill ===== >&2

jobs --no-such-option
echo jobs no-such-option $?
while kill -0 $$; do sleep 1; done 2>/dev/null&
(jobs >&- 2>/dev/null)
echo jobs output error $?
jobs %100
echo jobs no-such-job 1 $?
jobs %no_such_job
echo jobs no-such-job 2 $?
kill %while

echo ===== wait =====
echo ===== wait ===== >&2

wait --no-such-option
echo wait no-such-option $?
wait %100
echo wait no-such-job 1 $?
wait %no_such_job
echo wait no-such-job 2 $?

echo ===== times =====
echo ===== times ===== >&2

times --no-such-option
echo times no-such-option $?
times foo
echo times invalid operand $?
(times >&- 2>/dev/null)
echo times output error $?

echo ===== suspend =====
echo ===== suspend ===== >&2

suspend --no-such-option
echo suspend no-such-option $?
suspend foo
echo suspend invalid operand $?

echo ===== ulimit =====
echo ===== ulimit ===== >&2

if type ulimit 2>/dev/null | grep -q '^ulimit: a regular built-in'; then
    ulimit --no-such-option 2>/dev/null
    echo ulimit no-such-option $?
    ulimit -a -f
    echo ulimit invalid option $?
    (ulimit >&- 2>/dev/null)
    echo ulimit output error $?
    ulimit xxx
    echo ulimit invalid operand $?
    ulimit 0 0
    echo ulimit too many operands 1 $?
    ulimit -a 0
    echo ulimit too many operands 2 $?
else
    cat <<\END
ulimit no-such-option 2
ulimit invalid option 2
ulimit output error 1
ulimit invalid operand 2
ulimit too many operands 1 2
ulimit too many operands 2 2
END
    cat >&2 <<\END
ulimit: the -a option cannot be used with the -f option
ulimit: `xxx' is not a valid integer
ulimit: too many operands are specified
ulimit: no operand is expected
END
fi

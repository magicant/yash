# error.y.tst: yash-specific test of error handling
# vim: set ft=sh ts=8 sts=4 sw=4 noet:

# error tests for alias/unalias are in alias.y.tst
# error tests for array are in array.y.tst
# error tests for help are in help.y.tst
# error tests for fc/history are in history.y.tst
# error tests for pushd/popd/dirs are in dirstack.y.tst

echo ===== consequences of shell errors =====
echo ===== consequences of shell errors ===== >&2

$INVOKE $TESTEE <<\END
fi
echo not printed
END
echo syntax error $?

$INVOKE $TESTEE <<\END
.
echo special builtin syntax error $?
END

$INVOKE $TESTEE <<\END
getopts
echo non-special builtin syntax error $?
END

$INVOKE $TESTEE <<\END
exec 3>&-
{ exit >&3; } 2>/dev/null
echo special builtin redirection error $?
END

$INVOKE $TESTEE <<\END
exec 3>&-
{ cd . >&3; } 2>/dev/null
echo non-special builtin redirection error $?
END

$INVOKE $TESTEE <<\END
readonly ro=v
ro=v :
echo special builtin assignment error $?
END

$INVOKE $TESTEE <<\END
readonly ro=v
ro=v cd .
echo non-special builtin assignment error $?
END

$INVOKE $TESTEE <<\END
unset var
eval ${var?}
echo not printed
END
echo special builtin expansion error $?

$INVOKE $TESTEE <<\END
unset var
cd ${var?}
echo not printed
END
echo non-special builtin expansion error $?

$INVOKE $TESTEE <<\END
{ ./no/such/command; } 2>/dev/null
echo command not found error $?
END


echo ===== invocation =====
echo ===== invocation ===== >&2

$INVOKE $TESTEE -c
echo -c $?
$INVOKE $TESTEE -c -s
echo -c and -s 1 $?
$INVOKE $TESTEE -cs
echo -c and -s 2 $?
$INVOKE $TESTEE --cmdlin --stdi
echo -c and -s 3 $?
$INVOKE $TESTEE -o cmdlin -o stdi
echo -c and -s 4 $?
$INVOKE $TESTEE -c ++cmd-lin -o std_i <<\END
echo this is not an error
END
echo -c and -s 5 $?
$INVOKE $TESTEE --norc=file
echo invalid option argument 1 $?
$INVOKE $TESTEE --profile
echo missing argument profile $?
$INVOKE $TESTEE --rcfile
echo missing argument rcfile $?
$INVOKE $TESTEE --p
echo ambiguous option $?

echo ===== option =====
echo ===== option ===== >&2

set --cu
echo ambiguous option 1 $?
set --noh
echo ambiguous option 2 $?
set -o nolog +o nolog -o
echo missing argument $?
set -C-
echo invalid option 1 $?
set -aXb
echo invalid option 2 $?
set -o no-such-option
echo invalid option 3 $?
set --hel=1
echo invalid option argument 1 $?
set --hel=
echo invalid option argument 2 $?
set --help=1
echo invalid option argument 3 $?
set --help=
echo invalid option argument 4 $?

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
echo set no-such-option $?
(set >&- 2>/dev/null)
echo set output error $?

echo ===== cd =====
echo ===== cd ===== >&2

cd --no-such-option
echo cd no-such-option $?
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
(umask >&- 2>/dev/null)
echo umask output error $?

echo ===== typeset =====
echo ===== typeset ===== >&2

typeset --no-such-option
echo typeset no-such-option $?
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

echo ===== read =====
echo ===== read ===== >&2

read --no-such-option
echo read no-such-option $?
read
echo read operands missing $?
read read <&- 2>/dev/null
echo read input closed $?

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

echo ===== return =====
echo ===== return ===== >&2

(
return --no-such-option
echo return no-such-option $?
)
(
return foo
echo not printed
)
echo return invalid operand 1 $?
(
return -- -3
echo not printed
)
echo return invalid operand 2 $?
(
return 1 2
echo return too many operands $?
)

echo ===== break =====
echo ===== break ===== >&2

break --no-such-option
echo break no-such-option $?
(
break
echo break not in loop $?
)
(
break -i
echo break not in iteration $?
)
(
break -i foo
echo break invalid operand $?
)

echo ===== continue =====
echo ===== continue ===== >&2

continue --no-such-option
echo continue no-such-option $?
(
continue
echo continue not in loop $?
)
(
continue -i
echo continue not in iteration $?
)
(
continue -i foo
echo continue invalid operand $?
)

echo ===== eval =====
echo ===== eval ===== >&2

eval --no-such-option
echo eval no-such-option $?

echo ===== dot =====
echo ===== dot ===== >&2

. --no-such-option
echo dot no-such-option $?
.
echo dot operand missing $?
(
PATH=
. no_such_command 2>/dev/null
echo not printed
)
echo dot script not found in PATH $?
(
. "$TESTTMP/no/such/file" 2>/dev/null
echo not printed
)
echo dot file-not-found $?

echo ===== exec =====
echo ===== exec ===== >&2

(
exec --no-such-option
echo exec no-such-option $?
)
(
PATH=
exec no_such_command
echo not printed
) 2>/dev/null
echo exec command not found $?

echo ===== command =====
echo ===== command ===== >&2

command --no-such-option
echo command no-such-option $?
command -a foo
echo command invalid-option $?
(command -v command >&- 2>/dev/null)
echo command output error $?
(PATH=; command no_such_command)
echo command no-such-command 1 $?
echo =1= >&2
(PATH=; command -v no_such_command)
echo command no-such-command 2 $?
echo =2= >&2
(PATH=; command -V no_such_command)
echo command no-such-command 3 $?

echo ===== times =====
echo ===== times ===== >&2

times --no-such-option
echo times no-such-option $?
times foo
echo times invalid operand $?
(times >&- 2>/dev/null)
echo times output error $?

echo ===== exit =====
echo ===== exit ===== >&2

(
exit --no-such-option
echo exit no-such-option $?
)
(
exit foo
echo not printed
)
echo exit invalid operand 1 $?
(
exit -- -3
echo not printed
)
echo exit invalid operand 2 $?
(
exit 1 2
echo exit too many operands $?
)

echo ===== suspend =====
echo ===== suspend ===== >&2

suspend --no-such-option
echo suspend no-such-option $?
suspend foo
echo suspend invalid operand $?

echo ===== ulimit =====
echo ===== ulimit ===== >&2

if type ulimit 2>/dev/null | grep -q '^ulimit: regular builtin'; then
    ulimit --no-such-option 2>/dev/null
    echo ulimit no-such-option $?
    (ulimit >&- 2>/dev/null)
    echo ulimit output error $?
    ulimit xxx
    echo ulimit invalid operand $?
else
    cat <<\END
ulimit no-such-option 2
ulimit output error 1
ulimit invalid operand 2
END
    cat >&2 <<\END
ulimit: `xxx' is not a valid integer
END
fi

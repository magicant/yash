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


echo ===== help =====
echo ===== help ===== >&2

if type help 2>/dev/null | grep -q 'regular builtin'; then
	help --no-such-option
	echo help no-such-option $?
	help help help >&- 2>/dev/null
	echo help output error $?
else
	cat <<\END
help no-such-option 2
help output error 1
END
	cat >&2 <<\END
help: --no-such-option: invalid option
Usage:  help command...
END
fi

echo ===== set =====
echo ===== set ===== >&2

set --no-such-option
echo set no-such-option $?
set >&- 2>/dev/null
echo set output error $?

echo ===== cd =====
echo ===== cd ===== >&2

cd --no-such-option
echo cd no-such-option $?
cd ./no/such/dir 2>/dev/null
echo cd no-such-dir $?
cd /
(unset OLDPWD; cd -)
echo cd no OLDPWD $?
(unset HOME; cd)
echo cd no HOME $?
cd - >&- 2>/dev/null
echo cd output error $?

echo ===== pwd =====
echo ===== pwd ===== >&2

pwd --no-such-option
echo pwd no-such-option $?
pwd foo
echo pwd invalid operand $?
pwd >&- 2>/dev/null
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
hash >&- 2>/dev/null
echo hash output error $?

echo ===== umask =====
echo ===== umask ===== >&2

umask --no-such-option
echo umask no-such-option $?
umask >&- 2>/dev/null
echo umask output error $?

echo ===== pushd/popd/dirs =====
echo ===== pushd/popd/dirs ===== >&2

if type pushd 2>/dev/null | grep -q 'regular builtin'; then
	pushd --no-such-option
	echo pushd no-such-option $?
	pushd ./no/such/dir 2>/dev/null
	echo pushd no-such-dir $?
	pushd /
	pushd +5
	echo pushd index out of range $?
	pushd - >&- 2>/dev/null
	echo pushd output error $?
	popd --no-such-option
	echo popd no-such-option $?
	popd +5
	echo popd index out of range $?
	popd >/dev/null
	popd >&- 2>/dev/null
	echo popd output error $?
	popd
	echo popd dirstack empty $?
	dirs --no-such-option
	echo dirs no-such-option $?
	dirs >&- 2>/dev/null
	echo dirs output error $?
else
	cat <<\END
pushd no-such-option 2
pushd no-such-dir 1
pushd index out of range 1
pushd output error 0
popd no-such-option 2
popd index out of range 1
popd output error 0
popd dirstack empty 1
dirs no-such-option 2
dirs output error 1
END
	cat >&2 <<\END
pushd: --no-such-option: invalid option
Usage:  pushd [-L|-P] [dir]
pushd: +5: index out of range
popd: --no-such-option: invalid option
Usage:  popd [index]
popd: +5: index out of range
popd: directory stack is empty
dirs: --no-such-option: invalid option
Usage:  dirs [-cv] [index...]
END
fi

echo ===== alias/unalias =====
echo ===== alias/unalias ===== >&2

if type alias 2>/dev/null | grep -q 'semi-special builtin'; then
	alias --no-such-option
	echo alias no-such-option $?
	alias alias
	echo alias no-such-alias $?
	alias alias=alias
	alias >&- 2>/dev/null
	echo alias output error 1 $?
	alias alias >&- 2>/dev/null
	echo alias output error 2 $?
	alias -p alias >&- 2>/dev/null
	echo alias output error 3 $?
	unalias --no-such-option
	echo unalias no-such-option $?
	unalias alias
	unalias alias
	echo unalias no-such-alias $?
else
	cat <<\END
alias no-such-option 2
alias no-such-alias 1
alias output error 1 1
alias output error 2 1
alias output error 3 1
unalias no-such-option 2
unalias no-such-alias 1
END
	cat >&2 <<\END
alias: --no-such-option: invalid option
Usage:  alias [-gp] [name[=value]...]
alias: alias: no such alias
unalias: --no-such-option: invalid option
Usage:  unalias name[...]
        unalias -a
unalias: alias: no such alias
END
fi

echo ===== typeset =====
echo ===== typeset ===== >&2

typeset --no-such-option
echo typeset no-such-option $?
typeset >&- 2>/dev/null
echo typeset output error 1 $?
typeset -p >&- 2>/dev/null
echo typeset output error 2 $?
typeset -p PWD >&- 2>/dev/null
echo typeset output error 3 $?
func() { :; }
typeset -f >&- 2>/dev/null
echo typeset output error 4 $?
typeset -fp >&- 2>/dev/null
echo typeset output error 5 $?
typeset -fp func >&- 2>/dev/null
echo typeset output error 6 $?
typeset -r readonly=readonly
typeset -r readonly=readonly
echo typeset readonly $?

echo ===== array =====
echo ===== array ===== >&2

if type array 2>/dev/null | grep -q 'regular builtin'; then
	array --no-such-option
	echo array no-such-option $?
	array=(a 'b  b' c)
	array >&- 2>/dev/null
	echo array output error $?
else
	cat <<\END
array no-such-option 2
array output error 1
END
	cat >&2 <<\END
array: --no-such-option: invalid option
Usage:  array [name [value...]]
        array -d name [index...]
        array -i name index [value...]
        array -s name index value
END
fi

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
(
trap 'echo trap' INT TERM
trap >&- 2>/dev/null
echo trap output error 1 $?
trap -p >&- 2>/dev/null
echo trap output error 2 $?
trap -p INT >&- 2>/dev/null
echo trap output error 3 $?
)
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
kill -l >&- 2>/dev/null
echo kill output error 1 $?
kill -l HUP >&- 2>/dev/null
echo kill output error 2 $?

echo ===== jobs =====
echo ===== jobs ===== >&2

$INVOKE $TESTEE -m <<\END
jobs --no-such-option
echo jobs no-such-option $?
while kill -0 $$; do sleep 1; done &
jobs >&- 2>/dev/null
echo jobs output error $?
jobs %100
echo jobs no-such-job 1 $?
jobs %no_such_job
echo jobs no-such-job 2 $?
kill %while
END

echo ===== fg =====
echo ===== fg ===== >&2

$INVOKE $TESTEE -m <<\END
fg --no-such-option
echo fg no-such-option $?
exit 100 &
fg >&- 2>/dev/null
echo fg output error $?
fg %100
echo fg no-such-job 1 $?
fg %no_such_job
echo fg no-such-job 2 $?
END
$INVOKE $TESTEE -m --posix <<\END
exit 101 & exit 102 &
fg %1 %2
echo fg too many args $?
END

echo ===== bg =====
echo ===== bg ===== >&2

$INVOKE $TESTEE -m <<\END
bg --no-such-option
echo bg no-such-option $?
while kill -0 $$; do sleep 1; done &
bg >&- 2>/dev/null
echo bg output error $?
bg %100
echo bg no-such-job 1 $?
bg %no_such_job
echo bg no-such-job 2 $?
kill %1
END

echo ===== wait =====
echo ===== wait ===== >&2

wait --no-such-option
echo wait no-such-option $?
wait %100
echo wait no-such-job 1 $?
wait %no_such_job
echo wait no-such-job 2 $?

echo ===== fc/history =====
echo ===== fc/history ===== >&2

if type fc 2>/dev/null | grep -q 'semi-special builtin'; then
	fc --no-such-option
	echo fc no-such-option $?
	fc
	echo fc history-empty 1 $?
	fc -s
	echo fc history-empty 2 $?
	history -s 'entry' -s 'dummy 1' -s 'dummy 2'
	fc -l foo
	echo fc no-such-entry 1 $?
	fc foo
	echo fc no-such-entry 2 $?
	fc -s foo
	echo fc no-such-entry 3 $?
	history --no-such-option
	echo history no-such-option $?
	history >&- 2>/dev/null
	echo history output error $?
	history -d foo
	echo history no-such-entry $?
	history -r ./no/such/file $? 2>/dev/null
	echo history no-such-file 1 $?
	history -w ./no/such/file $? 2>/dev/null
	echo history no-such-file 2 $?
else
	cat <<\END
fc no-such-option 2
fc history-empty 1 1
fc history-empty 2 1
fc no-such-entry 1 1
fc no-such-entry 2 1
fc no-such-entry 3 1
history no-such-option 2
history output error 1
history no-such-entry 1
history no-such-file 1 1
history no-such-file 2 1
END
	cat >&2 <<\END
fc: --no-such-option: invalid option
Usage:  fc [-qr] [-e editor] [first [last]]
        fc -s [-q] [old=new] [first]
        fc -l [-nrv] [first [last]]
fc: history is empty
fc: history is empty
fc: no such entry beginning with `foo'
fc: no such entry beginning with `foo'
fc: no such entry beginning with `foo'
history: --no-such-option: invalid option
Usage:  history [-cF] [-d entry] [-s command] [-r file] [-w file] [n]
history: no such entry beginning with `foo'
END
fi

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
. ./no/such/file 2>/dev/null
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
command -v command >&- 2>/dev/null
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
times >&- 2>/dev/null
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

if type ulimit 2>/dev/null | grep -q 'regular builtin'; then
	ulimit --no-such-option 2>/dev/null
	echo ulimit no-such-option $?
	ulimit >&- 2>/dev/null
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

echo ===== printf =====
echo ===== printf ===== >&2

if type printf 2>/dev/null | grep -q 'regular builtin'; then
	printf --no-such-option
	echo printf no-such-option $?
	printf foo >&- 2>/dev/null
	echo printf output error $?
	printf
	echo printf operand missing $?
	printf '%d\n' foo 2>/dev/null
	echo printf invalid operand $?
	echo foo >&- 2>/dev/null
	echo echo output error $?
else
	cat <<\END
printf no-such-option 2
printf output error 1
printf operand missing 2
0
printf invalid operand 1
echo output error 1
END
	cat >&2 <<\END
printf: --no-such-option: invalid option
Usage:  printf format [value...]
Usage:  printf format [value...]
END
fi

echo ===== bindkey =====
echo ===== bindkey ===== >&2

if type bindkey 2>/dev/null | grep -q 'regular builtin'; then
	bindkey --no-such-option
	echo bindkey no-such-option $?
	bindkey -l >&- 2>/dev/null
	echo bindkey output error 1 $?
	bindkey -v >&- 2>/dev/null
	echo bindkey output error 2 $?
	bindkey
	echo bindkey operand missing $?
	bindkey -v '\\' no-such-command
	echo bindkey invalid operand 1 $?
	bindkey -v '' abort-line
	echo bindkey invalid operand 2 $?
else
	cat <<\END
bindkey no-such-option 2
bindkey output error 1 1
bindkey output error 2 1
bindkey operand missing 2
bindkey invalid operand 1 1
bindkey invalid operand 2 1
END
	cat >&2 <<\END
bindkey: --no-such-option: invalid option
Usage:  bindkey -aev [keyseq [command]]
        bindkey -l
bindkey: option not specified
Usage:  bindkey -aev [keyseq [command]]
        bindkey -l
bindkey: no-such-command: no such command
bindkey: cannot bind empty sequence
END
fi

# signal-y.tst: yash-specific test of signal handling
../checkfg || skip="true" # %SEQUENTIAL%

cat >eraseps <<\__END__
PS1= PS2=
__END__

# $1 = line no.
# $2 = signal name
test_interactive_subshell_signal_ignore() {
    testcase "$1" "SIG$2 kills interactive shell's subshell" -i \
	3<<__IN__ 4<<\__OUT__
(sh -c 'kill -s $2 \$PPID'; echo not reached)
echo -
__IN__
-
__OUT__
}

test_interactive_subshell_signal_ignore "$LINENO" INT
test_interactive_subshell_signal_ignore "$LINENO" QUIT
test_interactive_subshell_signal_ignore "$LINENO" TERM

# $1 = line no.
# $2 = signal name
test_job_controlling_subshell_signal_ignore() {
    testcase "$1" "SIG$2 stops job-controlling shell's subshell" -im \
	3<<__IN__ 4<<\__OUT__
(sh -c 'kill -s $2 \$PPID'; echo resumed)
echo -
fg >/dev/null
__IN__
-
resumed
__OUT__
}

test_job_controlling_subshell_signal_ignore "$LINENO" TTIN
test_job_controlling_subshell_signal_ignore "$LINENO" TTOU
test_job_controlling_subshell_signal_ignore "$LINENO" TSTP

# $1 = line no.
# $2 = signal name
test_noninteractive_job_controlling_shell_signal_kill() {
    testcase "$1" "SIG$2 kills non-interactive job-controlling shell" \
	3<<__IN__ 4<<__OUT__
"$TESTEE" -cm +i 'kill -s $2 \$\$'
kill -l \$?
__IN__
$2
__OUT__
}

test_noninteractive_job_controlling_shell_signal_kill "$LINENO" ABRT
test_noninteractive_job_controlling_shell_signal_kill "$LINENO" ALRM
test_noninteractive_job_controlling_shell_signal_kill "$LINENO" BUS
test_noninteractive_job_controlling_shell_signal_kill "$LINENO" FPE
test_noninteractive_job_controlling_shell_signal_kill "$LINENO" HUP
test_noninteractive_job_controlling_shell_signal_kill "$LINENO" ILL
test_noninteractive_job_controlling_shell_signal_kill "$LINENO" INT
test_noninteractive_job_controlling_shell_signal_kill "$LINENO" KILL
test_noninteractive_job_controlling_shell_signal_kill "$LINENO" PIPE
test_noninteractive_job_controlling_shell_signal_kill "$LINENO" QUIT
test_noninteractive_job_controlling_shell_signal_kill "$LINENO" SEGV
test_noninteractive_job_controlling_shell_signal_kill "$LINENO" TERM
test_noninteractive_job_controlling_shell_signal_kill "$LINENO" USR1
test_noninteractive_job_controlling_shell_signal_kill "$LINENO" USR2

# $1 = line no.
# $2 = signal name
test_noninteractive_job_controlling_shell_signal_ignore() {
    testcase "$1" -e 0 "SIG$2 spares non-interactive job-controlling shell" \
	3<<__IN__ 4</dev/null 5</dev/null
exec "$TESTEE" -cm +i 'kill -s $2 \$\$'
__IN__
}

test_noninteractive_job_controlling_shell_signal_ignore "$LINENO" CHLD
test_noninteractive_job_controlling_shell_signal_ignore "$LINENO" URG

# $1 = line no.
# $2 = signal name
test_interactive_job_controlling_shell_signal_kill() {
    testcase "$1" "SIG$2 kills interactive job-controlling shell" \
	3<<__IN__ 4<<__OUT__
"$TESTEE" -cim --norcfile 'kill -s $2 \$\$'
kill -l \$?
__IN__
$2
__OUT__
}

test_interactive_job_controlling_shell_signal_kill "$LINENO" ABRT
test_interactive_job_controlling_shell_signal_kill "$LINENO" ALRM
test_interactive_job_controlling_shell_signal_kill "$LINENO" BUS
test_interactive_job_controlling_shell_signal_kill "$LINENO" FPE
test_interactive_job_controlling_shell_signal_kill "$LINENO" HUP
test_interactive_job_controlling_shell_signal_kill "$LINENO" ILL
test_interactive_job_controlling_shell_signal_kill "$LINENO" KILL
test_interactive_job_controlling_shell_signal_kill "$LINENO" PIPE
test_interactive_job_controlling_shell_signal_kill "$LINENO" SEGV
test_interactive_job_controlling_shell_signal_kill "$LINENO" USR1
test_interactive_job_controlling_shell_signal_kill "$LINENO" USR2

# $1 = line no.
# $2 = signal name
test_interactive_job_controlling_shell_signal_ignore() {
    testcase "$1" -e 0 "SIG$2 spares interactive job-controlling shell" \
	3<<__IN__ 4</dev/null 5</dev/null
exec "$TESTEE" -cim --norcfile 'kill -s $2 \$\$'
__IN__
}

test_interactive_job_controlling_shell_signal_ignore "$LINENO" CHLD
test_interactive_job_controlling_shell_signal_ignore "$LINENO" INT
test_interactive_job_controlling_shell_signal_ignore "$LINENO" QUIT
test_interactive_job_controlling_shell_signal_ignore "$LINENO" TERM
test_interactive_job_controlling_shell_signal_ignore "$LINENO" URG

# $1 = line no.
# $2 = signal name
test_noninteractive_job_controlling_shell_job_signal_kill() {
    testcase "$1" "SIG$2 kills non-interactive job-controlling shell's job" \
	-m +i 3<<__IN__ 4<<__OUT__
(kill -s $2 0)
kill -l \$?
__IN__
$2
__OUT__
}

test_noninteractive_job_controlling_shell_job_signal_kill "$LINENO" ABRT
test_noninteractive_job_controlling_shell_job_signal_kill "$LINENO" ALRM
test_noninteractive_job_controlling_shell_job_signal_kill "$LINENO" BUS
test_noninteractive_job_controlling_shell_job_signal_kill "$LINENO" FPE
test_noninteractive_job_controlling_shell_job_signal_kill "$LINENO" HUP
test_noninteractive_job_controlling_shell_job_signal_kill "$LINENO" ILL
test_noninteractive_job_controlling_shell_job_signal_kill "$LINENO" INT
test_noninteractive_job_controlling_shell_job_signal_kill "$LINENO" KILL
test_noninteractive_job_controlling_shell_job_signal_kill "$LINENO" PIPE
test_noninteractive_job_controlling_shell_job_signal_kill "$LINENO" QUIT
test_noninteractive_job_controlling_shell_job_signal_kill "$LINENO" SEGV
test_noninteractive_job_controlling_shell_job_signal_kill "$LINENO" TERM
test_noninteractive_job_controlling_shell_job_signal_kill "$LINENO" USR1
test_noninteractive_job_controlling_shell_job_signal_kill "$LINENO" USR2

# $1 = line no.
# $2 = signal name
test_noninteractive_job_controlling_shell_job_signal_ignore() {
    testcase "$1" -e 0 \
	"SIG$2 spares non-interactive job-controlling shell's job" \
	-m +i 3<<__IN__ 4</dev/null 5</dev/null
(kill -s $2 0)
__IN__
}

test_noninteractive_job_controlling_shell_job_signal_ignore "$LINENO" CHLD
test_noninteractive_job_controlling_shell_job_signal_ignore "$LINENO" URG

# $1 = line no.
# $2 = signal name
test_noninteractive_job_controlling_shell_job_signal_stop() {
    testcase "$1" -e 0 \
	"SIG$2 stops non-interactive job-controlling shell's job" \
	-m +i 3<<__IN__ 4<<__OUT__
(kill -s $2 0; echo continued)
kill -l \$?
fg >/dev/null
__IN__
$2
continued
__OUT__
}

test_noninteractive_job_controlling_shell_job_signal_stop "$LINENO" TSTP
test_noninteractive_job_controlling_shell_job_signal_stop "$LINENO" TTIN
test_noninteractive_job_controlling_shell_job_signal_stop "$LINENO" TTOU
test_noninteractive_job_controlling_shell_job_signal_stop "$LINENO" STOP

# $1 = line no.
# $2 = signal name
test_interactive_job_controlling_shell_job_signal_kill() {
    testcase "$1" "SIG$2 kills interactive job-controlling shell's job" \
	-im 3<<__IN__ 4<<__OUT__
(kill -s $2 0)
kill -l \$?
__IN__
$2
__OUT__
}

test_interactive_job_controlling_shell_job_signal_kill "$LINENO" ABRT
test_interactive_job_controlling_shell_job_signal_kill "$LINENO" ALRM
test_interactive_job_controlling_shell_job_signal_kill "$LINENO" BUS
test_interactive_job_controlling_shell_job_signal_kill "$LINENO" FPE
test_interactive_job_controlling_shell_job_signal_kill "$LINENO" HUP
test_interactive_job_controlling_shell_job_signal_kill "$LINENO" ILL
test_interactive_job_controlling_shell_job_signal_kill "$LINENO" INT
test_interactive_job_controlling_shell_job_signal_kill "$LINENO" KILL
test_interactive_job_controlling_shell_job_signal_kill "$LINENO" PIPE
test_interactive_job_controlling_shell_job_signal_kill "$LINENO" QUIT
test_interactive_job_controlling_shell_job_signal_kill "$LINENO" SEGV
test_interactive_job_controlling_shell_job_signal_kill "$LINENO" TERM
test_interactive_job_controlling_shell_job_signal_kill "$LINENO" USR1
test_interactive_job_controlling_shell_job_signal_kill "$LINENO" USR2

# $1 = line no.
# $2 = signal name
test_interactive_job_controlling_shell_job_signal_ignore() {
    testcase "$1" -e 0 \
	"SIG$2 spares interactive job-controlling shell's job" \
	-im 3<<__IN__ 4</dev/null
(kill -s $2 0)
__IN__
}

test_interactive_job_controlling_shell_job_signal_ignore "$LINENO" CHLD
test_interactive_job_controlling_shell_job_signal_ignore "$LINENO" URG

# $1 = line no.
# $2 = signal name
test_interactive_job_controlling_shell_job_signal_stop() {
    testcase "$1" -e 0 \
	"SIG$2 stops interactive job-controlling shell's job" \
	-im 3<<__IN__ 4<<__OUT__
(kill -s $2 0; echo continued)
kill -l \$?
fg >/dev/null
__IN__
$2
continued
__OUT__
}

test_interactive_job_controlling_shell_job_signal_stop "$LINENO" TSTP
test_interactive_job_controlling_shell_job_signal_stop "$LINENO" TTIN
test_interactive_job_controlling_shell_job_signal_stop "$LINENO" TTOU
test_interactive_job_controlling_shell_job_signal_stop "$LINENO" STOP

test_oe 'SIGINT interrupts interactive shell' -i +m --rcfile=./eraseps
for i in 1 2 3; do
    echo $i
    sh -c 'kill -s INT $$'
    echo not reached
done
echo - >&2
for i in 4 5 6; do
    echo $i
    kill -s INT $$
    echo not reached
done
echo done
__IN__
1
4
done
__OUT__

-
__ERR__

# vim: set ft=sh ts=8 sts=4 sw=4 noet:

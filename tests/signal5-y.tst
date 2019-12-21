# signal5-y.tst: yash-specific test of signal handling, part 5
../checkfg || skip="true" # %REQUIRETTY%

# $1 = line no.
# $2 = signal name
test_noninteractive_job_controlling_shell_job_signal_kill() {
    testcase "$1" "SIG$2 kills non-interactive job-controlling shell's job" \
	-m +i 3<<__IN__ 4<<__OUT__ 5<&-
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
	-m +i 3<<__IN__ 4<<__OUT__ 5<&-
(kill -s $2 0; echo continued)
kill -l \$?
fg >/dev/null
__IN__
$2
continued
__OUT__
}

(
if "$use_valgrind"; then
    skip="true"
fi

test_noninteractive_job_controlling_shell_job_signal_stop "$LINENO" TSTP
test_noninteractive_job_controlling_shell_job_signal_stop "$LINENO" TTIN
test_noninteractive_job_controlling_shell_job_signal_stop "$LINENO" TTOU
test_noninteractive_job_controlling_shell_job_signal_stop "$LINENO" STOP

)

# vim: set ft=sh ts=8 sts=4 sw=4 noet:

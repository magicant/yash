# signal8-y.tst: yash-specific test of signal handling, part 8

# $1 = line no.
# $2 = signal name
test_noninteractive_non_job_controlling_shell_job_signal_kill() {
    testcase "$1" \
	"SIG$2 kills non-interactive non-job-controlling shell's job" \
	+im 3<<__IN__ 4<<__OUT__
"$TESTEE" -c 'kill -s $2 \$\$; echo not reached'
kill -l \$?
__IN__
$2
__OUT__
}

test_noninteractive_non_job_controlling_shell_job_signal_kill "$LINENO" ABRT
test_noninteractive_non_job_controlling_shell_job_signal_kill "$LINENO" ALRM
test_noninteractive_non_job_controlling_shell_job_signal_kill "$LINENO" BUS
test_noninteractive_non_job_controlling_shell_job_signal_kill "$LINENO" FPE
test_noninteractive_non_job_controlling_shell_job_signal_kill "$LINENO" HUP
test_noninteractive_non_job_controlling_shell_job_signal_kill "$LINENO" ILL
test_noninteractive_non_job_controlling_shell_job_signal_kill "$LINENO" INT
test_noninteractive_non_job_controlling_shell_job_signal_kill "$LINENO" KILL
test_noninteractive_non_job_controlling_shell_job_signal_kill "$LINENO" PIPE
test_noninteractive_non_job_controlling_shell_job_signal_kill "$LINENO" QUIT
test_noninteractive_non_job_controlling_shell_job_signal_kill "$LINENO" SEGV
test_noninteractive_non_job_controlling_shell_job_signal_kill "$LINENO" TERM
test_noninteractive_non_job_controlling_shell_job_signal_kill "$LINENO" USR1
test_noninteractive_non_job_controlling_shell_job_signal_kill "$LINENO" USR2

# $1 = line no.
# $2 = signal name
test_noninteractive_non_job_controlling_shell_job_signal_ignore() {
    testcase "$1" -e 0 \
	"SIG$2 spares non-interactive non-job-controlling shell's job" \
	+im 3<<__IN__ 4<<\__OUT__ 5</dev/null
"$TESTEE" -c 'kill -s $2 \$\$ && echo ok'
__IN__
ok
__OUT__
}

test_noninteractive_non_job_controlling_shell_job_signal_ignore "$LINENO" CHLD
test_noninteractive_non_job_controlling_shell_job_signal_ignore "$LINENO" URG

# vim: set ft=sh ts=8 sts=4 sw=4 noet:

# signal6-y.tst: yash-specific test of signal handling, part 6

# $1 = line no.
# $2 = signal name
test_noninteractive_non_job_controlling_shell_signal_kill() {
    testcase "$1" -e "$2" \
	"SIG$2 kills non-interactive non-job-controlling shell" \
	+im 3<<__IN__ 4</dev/null 5</dev/null
kill -s $2 \$\$
echo not reached
__IN__
}

test_noninteractive_non_job_controlling_shell_signal_kill "$LINENO" ABRT
test_noninteractive_non_job_controlling_shell_signal_kill "$LINENO" ALRM
test_noninteractive_non_job_controlling_shell_signal_kill "$LINENO" BUS
test_noninteractive_non_job_controlling_shell_signal_kill "$LINENO" FPE
test_noninteractive_non_job_controlling_shell_signal_kill "$LINENO" HUP
test_noninteractive_non_job_controlling_shell_signal_kill "$LINENO" ILL
test_noninteractive_non_job_controlling_shell_signal_kill "$LINENO" INT
test_noninteractive_non_job_controlling_shell_signal_kill "$LINENO" KILL
test_noninteractive_non_job_controlling_shell_signal_kill "$LINENO" PIPE
test_noninteractive_non_job_controlling_shell_signal_kill "$LINENO" QUIT
test_noninteractive_non_job_controlling_shell_signal_kill "$LINENO" SEGV
test_noninteractive_non_job_controlling_shell_signal_kill "$LINENO" TERM
test_noninteractive_non_job_controlling_shell_signal_kill "$LINENO" USR1
test_noninteractive_non_job_controlling_shell_signal_kill "$LINENO" USR2

# $1 = line no.
# $2 = signal name
test_noninteractive_non_job_controlling_shell_signal_ignore() {
    testcase "$1" -e 0 \
	"SIG$2 spares non-interactive non-job-controlling shell" \
	-s +im 3<<__IN__ 4</dev/null 5</dev/null
kill -s $2 \$\$
__IN__
}

test_noninteractive_non_job_controlling_shell_signal_ignore "$LINENO" CHLD
test_noninteractive_non_job_controlling_shell_signal_ignore "$LINENO" URG

# vim: set ft=sh ts=8 sts=4 sw=4 noet:

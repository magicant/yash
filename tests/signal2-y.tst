# signal2-y.tst: yash-specific test of signal handling, part 2
../checkfg || skip="true" # %REQUIRETTY%

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

(
if "$use_valgrind"; then
    skip="true"
fi

test_interactive_job_controlling_shell_job_signal_stop "$LINENO" TSTP
test_interactive_job_controlling_shell_job_signal_stop "$LINENO" TTIN
test_interactive_job_controlling_shell_job_signal_stop "$LINENO" TTOU
test_interactive_job_controlling_shell_job_signal_stop "$LINENO" STOP

)

# vim: set ft=sh ts=8 sts=4 sw=4 noet:

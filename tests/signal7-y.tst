# signal7-y.tst: yash-specific test of signal handling, part 7

cat >eraseps <<\__END__
PS1= PS2=
__END__

# $1 = line no.
# $2 = signal name
test_interactive_non_job_controlling_shell_signal_kill() {
    testcase "$1" -e "$2" "SIG$2 kills interactive non-job-controlling shell" \
	-i +m --rcfile=eraseps 3<<__IN__ 4</dev/null 5</dev/null
kill -s $2 \$\$
echo not reached
__IN__
}

test_interactive_non_job_controlling_shell_signal_kill "$LINENO" ABRT
test_interactive_non_job_controlling_shell_signal_kill "$LINENO" ALRM
test_interactive_non_job_controlling_shell_signal_kill "$LINENO" BUS
test_interactive_non_job_controlling_shell_signal_kill "$LINENO" FPE
test_interactive_non_job_controlling_shell_signal_kill "$LINENO" HUP
test_interactive_non_job_controlling_shell_signal_kill "$LINENO" ILL
test_interactive_non_job_controlling_shell_signal_kill "$LINENO" KILL
test_interactive_non_job_controlling_shell_signal_kill "$LINENO" PIPE
test_interactive_non_job_controlling_shell_signal_kill "$LINENO" SEGV
test_interactive_non_job_controlling_shell_signal_kill "$LINENO" USR1
test_interactive_non_job_controlling_shell_signal_kill "$LINENO" USR2

# $1 = line no.
# $2 = signal name
test_interactive_non_job_controlling_shell_signal_ignore() {
    testcase "$1" -e 0 "SIG$2 spares interactive non-job-controlling shell" \
	-i +m --rcfile=eraseps 3<<__IN__ 4</dev/null 5</dev/null
kill -s $2 \$\$
__IN__
}

test_interactive_non_job_controlling_shell_signal_ignore "$LINENO" CHLD
test_interactive_non_job_controlling_shell_signal_ignore "$LINENO" QUIT
test_interactive_non_job_controlling_shell_signal_ignore "$LINENO" TERM
test_interactive_non_job_controlling_shell_signal_ignore "$LINENO" URG

# vim: set ft=sh ts=8 sts=4 sw=4 noet:

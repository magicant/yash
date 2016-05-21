# signal-p.tst: test of signal handling for any POSIX-compliant shell
../checkfg || skip="true" # %SEQUENTIAL%

posix="true"

# $1 = line no.
# $2 = signal name
test_interactive_shell_signal_ignore() {
    testcase "$1" "interactive shell ignores SIG$2" -i 3<<__IN__ 4<<\__OUT__
kill -s $2 \$\$
echo -
__IN__
-
__OUT__
}

test_interactive_shell_signal_ignore "$LINENO" INT
test_interactive_shell_signal_ignore "$LINENO" QUIT
test_interactive_shell_signal_ignore "$LINENO" TERM

# $1 = line no.
# $2 = signal name
test_job_controlling_shell_signal_ignore() {
    testcase "$1" "job-controlling shell ignores SIG$2" -im \
	3<<__IN__ 4<<\__OUT__
kill -s $2 \$\$
echo -
__IN__
-
__OUT__
}

test_job_controlling_shell_signal_ignore "$LINENO" TTIN
test_job_controlling_shell_signal_ignore "$LINENO" TTOU
test_job_controlling_shell_signal_ignore "$LINENO" TSTP

test_oE 'traps are not handled until foreground job finishes'
trap 'echo trapped' USR1
(
    kill -s USR1 $$
    echo signal sent
)
__IN__
signal sent
trapped
__OUT__

# vim: set ft=sh ts=8 sts=4 sw=4 noet:

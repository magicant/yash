# run-test.sh: runs a set of test cases
# (C) 2015 magicant
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 2 of the License, or
# (at your option) any later version.
# 
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

# This script expects two operands.
# The first is the pathname to the testee, the shell that is to be tested.
# The second is the pathname to the test file that defines test cases.
# The result is output to a result file whose name is given by replacing the
# extension of the test file with ".trs".
# If any test case fails, it is also reported to the standard error.
# If the -r option is specified, intermediate files are not removed.
# The exit status is zero unless a critical error occurs. Failure of test cases
# does not cause the script to return non-zero.

set -Ceu
umask u+rwx

##### Some utility functions and aliases

eprintf() {
    printf "$@" >&2
}

# $1 = pathname
absolute()
case "$1" in
    (/*)
	printf '%s\n' "$1";;
    (*)
	printf '%s/%s' "${PWD%/}" "$1";;
esac

##### Script startup

exec <&- 3>&- 4>&- 5>&-

# require yash for alias support and extended exec built-in
if ! [ "${YASH_VERSION-}" ]; then
    eprintf '%s: must be run with yash\n' "$0"
    exit 64 # sysexits.h EX_USAGE
fi

# ensure correctness of $PWD
cd -L .

remove_work_dir="true"
while getopts r opt; do
    case $opt in
	(r)
	    remove_work_dir="false";;
	(*)
	    exit 64 # sysexits.h EX_USAGE
    esac
done
shift "$((OPTIND-1))"

testee="$(command -v "${1:?testee not specified}")"
test_file="${2:?test file not specified}"

exec >|"${test_file%.*}.trs"

unset -v CDPATH COLUMNS COMMAND_NOT_FOUND_HANDLER DIRSTACK ECHO_STYLE ENV
unset -v FCEDIT HANDLED HISTFILE HISTRMDUP HISTSIZE HOME IFS LINES MAIL
unset -v MAILCHECK MAILPATH NLSPATH OLDPWD OPTARG PROMPT_COMMAND
unset -v PS1 PS1R PS1S PS2 PS2R PS2S PS3 PS3R PS3S PS4 PS4R PS4S 
unset -v RANDOM TERM YASH_AFTER_CD YASH_LOADPATH YASH_LE_TIMEOUT YASH_VERSION
unset -v A B C D E F G H I J K L M N O P Q R S T U V W X Y Z _
unset -v a b c d e f g h i j k l m n o p q r s t u v w x y z
unset -v posix skip
export LC_ALL=C
export -X LINENO OPTIND

##### Prepare temporary directory

work_dir="tmp.$$"

rm_work_dir()
if "$remove_work_dir"; then
    rm -fr "$work_dir"
fi

trap rm_work_dir EXIT
trap 'rm_work_dir; trap - INT;  kill -INT  $$' INT
trap 'rm_work_dir; trap - TERM; kill -TERM $$' TERM
trap 'rm_work_dir; trap - QUIT; kill -QUIT $$' QUIT

mkdir "$work_dir"

##### Some more utilities

{
    if diff -U 10000 /dev/null /dev/null; then
	diff_opt='-U 10000'
    elif diff -C 10000 /dev/null /dev/null; then
	diff_opt='-C 10000'
    else
	diff_opt=''
    fi
} >/dev/null 2>&1

setup_script=""

# Add a setup script that is run before each test case
# If the first argument is "-" or omitted, the script is read from stdin.
# If the first argument is "-d", the default utility functions are added.
# Otherwise, the first argument is added as the script.
setup() {
    case "${1--}" in
	(-)
	    setup "$(cat)"
	    ;;
	(-d)
	    setup <<\END
_empty= _sp=' ' _tab='	' _nl='
'
echoraw() {
    printf '%s\n' "$*"
}
bracket() {
    if [ $# -gt 0 ]; then printf '[%s]' "$@"; fi
    echo
}
END
	    ;;
	(*)
	    setup_script="$setup_script
$1"
	    ;;
    esac
}

# Invokes the testee.
# If the "posix" variable is defined non-empty, the testee is invoked as "sh".
testee() (
    exec ${posix:+-a sh} "$testee" "$@"
)

# The test case runner.
#
# Contents of file descriptor 3 are passed to the standard input of a newly
# invoked testee using a temporary file.
# Contents of file descriptor 4 and 5 are compared to the actual output from
# the standard output and error of the testee, respectively, if those file
# descriptors are open. If they differ from the expected, the test case fails.
#
# The first argument is treated as the line number where the test case appears
# in the test file. As remaining arguments, options and operands may follow.
#
# If the "-d" option is specified, the test case fails unless the actual output
# to the standard error is non-empty. File descriptor 5 is ignored.
#
# If the "-e <expected_exit_status>" option is specified, the exit status of
# the testee is also checked. If the actual exit status differs from the
# expected, the test case fails. If <expected_exit_status> is "n", the expected
# is any non-zero exit status.
#
# The first operand is used as the name of the test case.
# The remaining operands are passed as arguments to the testee.
#
# If the "skip" variable is defined non-empty, the test case is skipped.
testcase() {
    test_lineno="${1:?line number unspecified}"
    shift 1
    OPTIND=1
    diagnostic_required="false"
    expected_exit_status=""
    while getopts de: opt; do
	case $opt in
	    (d)
		diagnostic_required="true";;
	    (e)
		expected_exit_status="$OPTARG";;
	    (*)
		return 64 # sysexits.h EX_USAGE
	esac
    done
    shift "$((OPTIND-1))"
    test_case_name="${1:?unnamed test case \($test_file:$test_lineno\)}"
    shift 1

    log_stdout() {
	printf '%%%%%% %s: %s:%d: %s\n' \
	    "$1" "$test_file" "$test_lineno" "$test_case_name"
    }

    in_file="$test_lineno.in"
    out_file="$test_lineno.out"
    err_file="$test_lineno.err"

    # prepare input file
    {
	if [ "$setup_script" ]; then
	    printf '%s\n' "$setup_script"
	fi
	cat <&3
    } >"$in_file"

    if [ "${skip-}" ]; then
	log_stdout SKIPPED
	echo
	return
    fi

    # run the testee
    log_stdout START
    set +e
    testee "$@" <"$in_file" >"$out_file" 2>"$err_file" 3>&- 4>&- 5>&-
    actual_exit_status="$?"
    set -e

    failed="false"

    # check exit status
    exit_status_fail() {
	failed="true"
	eprintf '%s:%d: %s: exit status mismatch\n' \
	    "$test_file" "$test_lineno" "$test_case_name"
    }
    if [ "$expected_exit_status" ]; then
	if [ "$expected_exit_status" = n ]; then
	    printf '%% exit status: expected=non-zero actual=%d\n\n' \
		"$actual_exit_status"
	    if [ "$actual_exit_status" -eq 0 ]; then
		exit_status_fail
	    fi
	else
	    printf '%% exit status: expected=%d actual=%d\n\n' \
		"$expected_exit_status" "$actual_exit_status"
	    if [ "$actual_exit_status" -ne "$expected_exit_status" ]; then
		exit_status_fail
	    fi
	fi
    fi

    # check standard output
    if { exec <&4; } 2>/dev/null; then
	printf '%% standard output diff:\n'
	if ! diff $diff_opt - "$out_file"; then
	    failed="true"
	    eprintf '%s:%d: %s: standard output mismatch\n' \
		"$test_file" "$test_lineno" "$test_case_name"
	fi
	echo
    fi

    # check standard error
    if "$diagnostic_required"; then
	printf '%% standard error (expecting non-empty output):\n'
	cat "$err_file"
	if ! [ -s "$err_file" ]; then
	    failed="true"
	    eprintf '%s:%d: %s: standard error mismatch\n' \
		"$test_file" "$test_lineno" "$test_case_name"
	fi
	echo
    elif { exec <&5; } 2>/dev/null; then
	printf '%% standard error diff:\n'
	if ! diff $diff_opt - "$err_file"; then
	    failed="true"
	    eprintf '%s:%d: %s: standard error mismatch\n' \
		"$test_file" "$test_lineno" "$test_case_name"
	fi
	echo
    fi

    if "$failed"; then
	log_stdout FAILED
    else
	log_stdout PASSED
    fi
    echo
}

alias test_x='testcase "$LINENO" 3<<\__IN__'
alias test_o='testcase "$LINENO" 3<<\__IN__ 4<<\__OUT__'
alias test_O='testcase "$LINENO" 3<<\__IN__ 4</dev/null'
alias test_e='testcase "$LINENO" 3<<\__IN__ 5<<\__ERR__'
alias test_oe='testcase "$LINENO" 3<<\__IN__ 4<<\__OUT__ 5<<\__ERR__'
alias test_Oe='testcase "$LINENO" 3<<\__IN__ 4</dev/null 5<<\__ERR__'
alias test_E='testcase "$LINENO" 3<<\__IN__ 5</dev/null'
alias test_oE='testcase "$LINENO" 3<<\__IN__ 4<<\__OUT__ 5</dev/null'
alias test_OE='testcase "$LINENO" 3<<\__IN__ 4</dev/null 5</dev/null'

##### Run test

# The test is run in a subshell. The main shell waits for a possible trap and
# lastly removes the temporary directory.
(
abs_test_file="$(absolute "$test_file")"
cd "$work_dir"

export TESTEE="$testee"

. "$abs_test_file"
)

# vim: set ts=8 sts=4 sw=4 noet:

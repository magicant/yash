# test-y.tst: yash-specific test of the test built-in

if ! testee -c 'command -bv test' >/dev/null; then
    skip="true"
fi

umask u=rwx,go=

>file
ln -s file filelink
ln -s _no_such_file_ brokenlink
ln file hardlink

touch -t 200001010000 older
touch -t 200101010000 newer
touch -a -t 200101010000 old; touch -m -t 200001010000 old
touch -a -t 200001010000 new; touch -m -t 200101010000 new

# $1 = $LINENO, $2 = expected exit status, $3... = expression
assert() (
    setup <<\__END__
    test "$@"
    result_test=$?
    [ "$@" ]
    result_bracket=$?
    case "$result_test" in ("$result_bracket")
	exit "$result_bracket"
    esac
    printf 'result_test=%d result_bracket=%d\n' "$result_test" "$result_bracket"
    exit 100
__END__

    lineno="$1"
    expected_exit_status="$2"
    shift 2
    testcase "$lineno" -e "$expected_exit_status" "test $*" -s -- "$@" \
	3</dev/null 4<&3 5<&3
)

alias assert_true='assert "$LINENO" 0'
alias assert_false='assert "$LINENO" 1'

(
mkdir dir sticky
if ! { chmod a-t dir && chmod a+t sticky; } then
    skip="true"
fi
ln -s sticky stickylink

assert_true -k
assert_true -k sticky
assert_true -k stickylink
assert_false -k file
assert_false -k filelink
assert_false -k dir
assert_false -k dirlink
assert_false -k ./_no_such_file_
assert_false -k brokenlink
)

assert_true -G
# Tests for the -G operator is missing
assert_false -G ./_no_such_file_
assert_false -G brokenlink

assert_true -N
assert_true -N new
assert_false -N old
assert_false -N ./_no_such_file_
assert_false -N brokenlink

assert_true -O
# Tests for the -O operator is missing
assert_false -O ./_no_such_file_
assert_false -O brokenlink

assert_true -o
assert_false -o allexpo
assert_false -o allexport
assert_false -o all-_export
assert_false -o allexportttttttt
assert_true -o \?allexpo
assert_true -o \?allexport
assert_true -o \?all-_export
assert_false -o \?allexportttttttt
(
setup 'set -o allexport'
assert_true -o allexpo
assert_true -o allexport
assert_true -o all-_export
assert_false -o allexportttttttt
assert_true -o \?allexpo
assert_true -o \?allexport
assert_true -o \?all-_export
assert_false -o \?allexportttttttt
)

assert_false -o tify
assert_false -o notify
assert_true -o nonotify
assert_true -o n-o-n-otify
assert_false -o \?tify
assert_true -o \?notify
assert_true -o \?nonotify
assert_true -o \?n-o-n-otify
(
setup 'set -o notify'
assert_false -o tify
assert_true -o notify
assert_false -o nonotify
assert_false -o n-o-n-otify
assert_false -o \?tify
assert_true -o \?notify
assert_true -o \?nonotify
assert_true -o \?n-o-n-otify
)

assert_true XXXXX -ot newer
assert_false XXXXX -ot XXXXX
assert_false newer -ot XXXXX
assert_true older -ot newer
assert_false newer -ot newer
assert_false newer -ot older

assert_false XXXXX -nt newer
assert_false XXXXX -nt XXXXX
assert_true newer -nt XXXXX
assert_false older -nt newer
assert_false older -nt older
assert_true newer -nt older

assert_false XXXXX -ef newer
assert_false XXXXX -ef XXXXX
assert_false newer -ef XXXXX
assert_false older -ef newer
assert_true older -ef older
assert_false newer -ef older
assert_true file -ef hardlink
assert_false file -ef newer

assert_true "" == ""
assert_true 1 == 1
assert_true abcde == abcde
assert_false 0 == 1
assert_false abcde == 12345
assert_true ! == !
assert_true == == ==
assert_false "(" == ")"

# The behavior of the ===, !==, <, <=, >, >= operators cannot be fully tested.
assert_true "" === ""
assert_true 1 === 1
assert_true abcde === abcde
assert_false 0 === 1
assert_false abcde === 12345
assert_true ! === !
assert_true === === ===
assert_false "(" === ")"

assert_false "" !== ""
assert_false 1 !== 1
assert_false abcde !== abcde
assert_true 0 !== 1
assert_true abcde !== 12345
assert_false ! !== !
assert_false !== !== !==
assert_true "(" !== ")"

assert_false 11 '<' 100
assert_false 11 '<' 11
assert_true 100 '<' 11

assert_false 11 '<=' 100
assert_true 11 '<=' 11
assert_true 100 '<=' 11

assert_true 11 '>' 100
assert_false 11 '>' 11
assert_false 100 '>' 11

assert_true 11 '>=' 100
assert_true 11 '>=' 11
assert_false 100 '>=' 11

assert_true  abc123xyz     =~ 'c[[:digit:]]*x'
assert_false -axyzxyzaxyz- =~ 'c[[:digit:]]*x'
assert_true  -axyzxyzaxyz- =~ '-(a|xyz)*-'
assert_false abc123xyz     =~ '-(a|xyz)*-'

assert_true "" -veq ""
assert_true 0 -veq 0
assert_false 0 -veq 1
assert_false 1 -veq 0
assert_true 01 -veq 0001
assert_true .%=01 -veq .%=0001
assert_true 0.01.. -veq 0.1..
assert_false 0.01.0 -veq 0.1.

assert_false "" -vne ""
assert_false 0 -vne 0
assert_true 0 -vne 1
assert_true 1 -vne 0

assert_false "" -vgt ""
assert_false 0 -vgt 0
assert_false 0 -vgt 1
assert_true 1 -vgt 0

assert_true "" -vge ""
assert_true 0 -vge 0
assert_false 0 -vge 1
assert_true 1 -vge 0

assert_false "" -vlt ""
assert_false 0 -vlt 0
assert_true 0 -vlt 1
assert_false 1 -vlt 0
assert_false 0.01.0 -vlt 0.1..
assert_false 0.01.0 -vlt 0.1.:

assert_true "" -vle ""
assert_true 0 -vle 0
assert_true 0 -vle 1
assert_false 1 -vle 0
assert_true 02 -vle 0100
assert_true .%=02 -vle .%=0100
assert_false 0.01.0 -vle 0.1.a0
assert_true 1.2.3 -vle 1.3.2
assert_true -2 -vle -3

assert_true 1 -a "(" 1 = 0 -o "(" 2 = 2 ")" ")" -a "(" = ")"
assert_true -n = -o -o -n = -n  # ( -n = -o ) -o ( -n = -n )
assert_true -n = -a -n = -n     # ( -n = ) -a ( -n = -n )

test_Oe -e 2 'invalid binary operator'
test 1 2
__IN__
test: `1' is not a unary operator
__ERR__
#'
#`

test_Oe -e 2 'invalid binary operator'
test 1 2 3
__IN__
test: `2' is not a binary operator
__ERR__
#'
#`

# vim: set ft=sh ts=8 sts=4 sw=4 noet:

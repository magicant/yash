# builtins-y.tst: yash-specific test of built-ins' attributes

(
posix="true"

# $1 = line no.
# $2 = built-in name
test_special_builtin_syntax() {
    testcase "$1" -d \
    "argument syntax error on special built-in $2 kills non-interactive shell" \
	3<<__IN__ 4</dev/null
$2 --no-such-option--
printf 'not reached\n'
__IN__
}

# $1 = line no.
# $2 = built-in name
test_special_builtin_syntax_s() {
    testcase "$1" -d \
    "argument syntax error on special built-in $2 in subshell" \
	3<<__IN__ 4<<\__OUT__
($2 --no-such-option--; echo not reached)
echo \$?
__IN__
2
__OUT__
}

# $1 = line no.
# $2 = built-in name
test_special_builtin_syntax_i() {
    testcase "$1" -d \
    "argument syntax error on special built-in $2 spares interactive shell" \
	-i +m 3<<__IN__ 4<<\__OUT__
$2 --no-such-option--
echo \$?
__IN__
2
__OUT__
}

# No argument syntax error in special built-in colon
# test_special_builtin_syntax   "$LINENO" :
# test_special_builtin_syntax_s "$LINENO" :
# test_special_builtin_syntax_i "$LINENO" :
test_special_builtin_syntax   "$LINENO" .
test_special_builtin_syntax_s "$LINENO" .
test_special_builtin_syntax_i "$LINENO" .
test_special_builtin_syntax   "$LINENO" break
test_special_builtin_syntax_s "$LINENO" break
test_special_builtin_syntax_i "$LINENO" break
test_special_builtin_syntax   "$LINENO" continue
test_special_builtin_syntax_s "$LINENO" continue
test_special_builtin_syntax_i "$LINENO" continue
test_special_builtin_syntax   "$LINENO" eval
test_special_builtin_syntax_s "$LINENO" eval
test_special_builtin_syntax_i "$LINENO" eval
test_special_builtin_syntax   "$LINENO" exec
test_special_builtin_syntax_s "$LINENO" exec
test_special_builtin_syntax_i "$LINENO" exec
test_special_builtin_syntax   "$LINENO" exit
test_special_builtin_syntax_s "$LINENO" exit
test_special_builtin_syntax_i "$LINENO" exit
test_special_builtin_syntax   "$LINENO" export
test_special_builtin_syntax_s "$LINENO" export
test_special_builtin_syntax_i "$LINENO" export
test_special_builtin_syntax   "$LINENO" readonly
test_special_builtin_syntax_s "$LINENO" readonly
test_special_builtin_syntax_i "$LINENO" readonly
test_special_builtin_syntax   "$LINENO" return
test_special_builtin_syntax_s "$LINENO" return
test_special_builtin_syntax_i "$LINENO" return
test_special_builtin_syntax   "$LINENO" set
test_special_builtin_syntax_s "$LINENO" set
test_special_builtin_syntax_i "$LINENO" set
test_special_builtin_syntax   "$LINENO" shift
test_special_builtin_syntax_s "$LINENO" shift
test_special_builtin_syntax_i "$LINENO" shift
test_special_builtin_syntax   "$LINENO" times
test_special_builtin_syntax_s "$LINENO" times
test_special_builtin_syntax_i "$LINENO" times
test_special_builtin_syntax   "$LINENO" trap
test_special_builtin_syntax_s "$LINENO" trap
test_special_builtin_syntax_i "$LINENO" trap
test_special_builtin_syntax   "$LINENO" unset
test_special_builtin_syntax_s "$LINENO" unset
test_special_builtin_syntax_i "$LINENO" unset

# $1 = line no.
# $2 = built-in name
test_nonspecial_builtin_syntax() (
    if ! testee -c "set +o posix; command -bv $2" >/dev/null; then
	skip="true"
    fi

    testcase "$1" -d \
	"argument syntax error on non-special built-in $2 spares shell" \
	3<<__IN__ 4<<\__OUT__
# -l and -n are mutually exclusive for the kill built-in.
# Four arguments are too many for the test built-in.
$2 -l -n --no-such-option-- -
echo \$?
__IN__
2
__OUT__
)

test_nonspecial_builtin_syntax "$LINENO" [
test_nonspecial_builtin_syntax "$LINENO" alias
# Non-standard built-in array skipped
# test_nonspecial_builtin_syntax "$LINENO" array
test_nonspecial_builtin_syntax "$LINENO" bg
# Non-standard built-in bindkey skipped
# test_nonspecial_builtin_syntax "$LINENO" bindkey
test_nonspecial_builtin_syntax "$LINENO" cd
test_nonspecial_builtin_syntax "$LINENO" command
# Non-standard built-in complete skipped
# test_nonspecial_builtin_syntax "$LINENO" complete
# Non-standard built-in dirs skipped
# test_nonspecial_builtin_syntax "$LINENO" dirs
# Non-standard built-in disown skipped
# test_nonspecial_builtin_syntax "$LINENO" disown
# No argument syntax error in non-special built-in echo
# test_nonspecial_builtin_syntax "$LINENO" echo
# No argument syntax error in non-special built-in false
# test_nonspecial_builtin_syntax "$LINENO" false
test_nonspecial_builtin_syntax "$LINENO" fc
test_nonspecial_builtin_syntax "$LINENO" fg
test_nonspecial_builtin_syntax "$LINENO" getopts
test_nonspecial_builtin_syntax "$LINENO" hash
# Non-standard built-in help skipped
# test_nonspecial_builtin_syntax "$LINENO" help
# Non-standard built-in history skipped
# test_nonspecial_builtin_syntax "$LINENO" history
test_nonspecial_builtin_syntax "$LINENO" jobs
test_nonspecial_builtin_syntax "$LINENO" kill
# Non-standard built-in popd skipped
# test_nonspecial_builtin_syntax "$LINENO" popd
test_nonspecial_builtin_syntax "$LINENO" printf
# Non-standard built-in pushd skipped
# test_nonspecial_builtin_syntax "$LINENO" pushd
test_nonspecial_builtin_syntax "$LINENO" pwd
test_nonspecial_builtin_syntax "$LINENO" read
# Non-standard built-in suspend skipped
# test_nonspecial_builtin_syntax "$LINENO" suspend
test_nonspecial_builtin_syntax "$LINENO" test
# No argument syntax error in non-special built-in true
# test_nonspecial_builtin_syntax "$LINENO" true
test_nonspecial_builtin_syntax "$LINENO" type
# Non-standard built-in typeset skipped
# test_nonspecial_builtin_syntax "$LINENO" typeset
test_nonspecial_builtin_syntax "$LINENO" ulimit
test_nonspecial_builtin_syntax "$LINENO" umask
test_nonspecial_builtin_syntax "$LINENO" unalias
test_nonspecial_builtin_syntax "$LINENO" wait

# $1 = line no.
# $2 = built-in name
test_nonspecial_builtin_redirect() {
    testcase "$1" -d \
	"redirection error on non-special built-in $2 spares shell" \
	3<<__IN__ 4<<\__OUT__
$2 <_no_such_file_
echo \$?
__IN__
2
__OUT__
}

test_nonspecial_builtin_redirect "$LINENO" [
test_nonspecial_builtin_redirect "$LINENO" alias
test_nonspecial_builtin_redirect "$LINENO" array
test_nonspecial_builtin_redirect "$LINENO" bg
test_nonspecial_builtin_redirect "$LINENO" bindkey
test_nonspecial_builtin_redirect "$LINENO" cat # example of external command
test_nonspecial_builtin_redirect "$LINENO" cd
test_nonspecial_builtin_redirect "$LINENO" command
test_nonspecial_builtin_redirect "$LINENO" complete
test_nonspecial_builtin_redirect "$LINENO" dirs
test_nonspecial_builtin_redirect "$LINENO" disown
test_nonspecial_builtin_redirect "$LINENO" echo
test_nonspecial_builtin_redirect "$LINENO" false
test_nonspecial_builtin_redirect "$LINENO" fc
test_nonspecial_builtin_redirect "$LINENO" fg
test_nonspecial_builtin_redirect "$LINENO" getopts
test_nonspecial_builtin_redirect "$LINENO" hash
test_nonspecial_builtin_redirect "$LINENO" help
test_nonspecial_builtin_redirect "$LINENO" history
test_nonspecial_builtin_redirect "$LINENO" jobs
test_nonspecial_builtin_redirect "$LINENO" kill
test_nonspecial_builtin_redirect "$LINENO" popd
test_nonspecial_builtin_redirect "$LINENO" printf
test_nonspecial_builtin_redirect "$LINENO" pushd
test_nonspecial_builtin_redirect "$LINENO" pwd
test_nonspecial_builtin_redirect "$LINENO" read
test_nonspecial_builtin_redirect "$LINENO" suspend
test_nonspecial_builtin_redirect "$LINENO" test
test_nonspecial_builtin_redirect "$LINENO" true
test_nonspecial_builtin_redirect "$LINENO" type
test_nonspecial_builtin_redirect "$LINENO" typeset
test_nonspecial_builtin_redirect "$LINENO" ulimit
test_nonspecial_builtin_redirect "$LINENO" umask
test_nonspecial_builtin_redirect "$LINENO" unalias
test_nonspecial_builtin_redirect "$LINENO" wait
test_nonspecial_builtin_redirect "$LINENO" ./_no_such_command_

# $1 = line no.
# $2 = built-in name
test_nonspecial_builtin_assign() {
    testcase "$1" -d \
	"assignment error on non-special built-in $2 spares shell" \
	3<<__IN__ 4<<\__OUT__
readonly a=a
a=b $2
echo \$?
__IN__
2
__OUT__
}

test_nonspecial_builtin_assign "$LINENO" [
test_nonspecial_builtin_assign "$LINENO" alias
test_nonspecial_builtin_assign "$LINENO" array
test_nonspecial_builtin_assign "$LINENO" bg
test_nonspecial_builtin_assign "$LINENO" bindkey
test_nonspecial_builtin_assign "$LINENO" cat # example of external command
test_nonspecial_builtin_assign "$LINENO" cd
test_nonspecial_builtin_assign "$LINENO" command
test_nonspecial_builtin_assign "$LINENO" complete
test_nonspecial_builtin_assign "$LINENO" dirs
test_nonspecial_builtin_assign "$LINENO" disown
test_nonspecial_builtin_assign "$LINENO" echo
test_nonspecial_builtin_assign "$LINENO" false
test_nonspecial_builtin_assign "$LINENO" fc
test_nonspecial_builtin_assign "$LINENO" fg
test_nonspecial_builtin_assign "$LINENO" getopts
test_nonspecial_builtin_assign "$LINENO" hash
test_nonspecial_builtin_assign "$LINENO" help
test_nonspecial_builtin_assign "$LINENO" history
test_nonspecial_builtin_assign "$LINENO" jobs
test_nonspecial_builtin_assign "$LINENO" kill
test_nonspecial_builtin_assign "$LINENO" popd
test_nonspecial_builtin_assign "$LINENO" printf
test_nonspecial_builtin_assign "$LINENO" pushd
test_nonspecial_builtin_assign "$LINENO" pwd
test_nonspecial_builtin_assign "$LINENO" read
test_nonspecial_builtin_assign "$LINENO" suspend
test_nonspecial_builtin_assign "$LINENO" test
test_nonspecial_builtin_assign "$LINENO" true
test_nonspecial_builtin_assign "$LINENO" type
test_nonspecial_builtin_assign "$LINENO" typeset
test_nonspecial_builtin_assign "$LINENO" ulimit
test_nonspecial_builtin_assign "$LINENO" umask
test_nonspecial_builtin_assign "$LINENO" unalias
test_nonspecial_builtin_assign "$LINENO" wait
test_nonspecial_builtin_assign "$LINENO" ./_no_such_command_

)

# vim: set ft=sh ts=8 sts=4 sw=4 noet:

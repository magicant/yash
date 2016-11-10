# error-p.tst: test of error conditions for any POSIX-compliant shell

posix="true"

test_O -d -e n 'syntax error kills non-interactive shell'
fi
echo not reached
__IN__

test_O -d -e n 'syntax error in eval kills non-interactive shell'
eval fi
echo not reached
__IN__

test_o -d 'syntax error in subshell'
(eval fi; echo not reached)
[ $? -ne 0 ]
echo $?
__IN__
0
__OUT__

test_o -d 'syntax error spares interactive shell' -i +m
fi
echo reached
__IN__
reached
__OUT__

test_O -d -e n 'expansion error kills non-interactive shell'
unset a
echo ${a?}
echo not reached
__IN__

test_o -d 'expansion error in subshell'
unset a
(echo ${a?}; echo not reached)
[ $? -ne 0 ]
echo $?
__IN__
0
__OUT__

test_o -d 'expansion error spares interactive shell' -i +m
unset a
echo ${a?}
[ $? -ne 0 ]
echo $?
__IN__
0
__OUT__

test_O -d -e 127 'command not found'
./_no_such_command_
__IN__

###############################################################################

# $1 = line no.
# $2 = built-in name
test_special_builtin_assign() {
    testcase "$1" -d \
	"assignment error on special built-in $2 kills non-interactive shell" \
	3<<__IN__ 4</dev/null
readonly a=a
a=b $2
printf 'not reached\n'
__IN__
}

# $1 = line no.
# $2 = built-in name
test_special_builtin_assign_s() {
    testcase "$1" -d \
	"assignment error on special built-in $2 in subshell" \
	3<<__IN__ 4<<\__OUT__
readonly a=a
(a=b $2; echo not reached)
[ \$? -ne 0 ]
echo \$?
__IN__
0
__OUT__
}

# $1 = line no.
# $2 = built-in name
test_special_builtin_assign_i() {
    testcase "$1" -d \
	"assignment error on special built-in $2 spares interactive shell" \
	-i +m 3<<__IN__ 4<<\__OUT__
readonly a=a
a=b $2
printf 'reached\n'
__IN__
reached
__OUT__
}

# $1 = line no.
# $2 = built-in name
test_special_builtin_redirect() {
    testcase "$1" -d \
	"redirection error on special built-in $2 kills non-interactive shell" \
	3<<__IN__ 4</dev/null
$2 <_no_such_file_
printf 'not reached\n'
__IN__
}

# $1 = line no.
# $2 = built-in name
test_special_builtin_redirect_s() {
    testcase "$1" -d \
	"redirection error on special built-in $2 in subshell" \
	3<<__IN__ 4<<\__OUT__
($2 <_no_such_file_; echo not reached)
[ \$? -ne 0 ]
echo \$?
__IN__
0
__OUT__
}

# $1 = line no.
# $2 = built-in name
test_special_builtin_redirect_i() {
    testcase "$1" -d \
	"redirection error on special built-in $2 spares interactive shell" \
	-i +m 3<<__IN__ 4<<\__OUT__
$2 <_no_such_file_
printf 'reached\n'
__IN__
reached
__OUT__
}

test_special_builtin_assign     "$LINENO" :
test_special_builtin_assign_s   "$LINENO" :
test_special_builtin_assign_i   "$LINENO" :
test_special_builtin_redirect   "$LINENO" :
test_special_builtin_redirect_s "$LINENO" :
test_special_builtin_redirect_i "$LINENO" :
test_special_builtin_assign     "$LINENO" .
test_special_builtin_assign_s   "$LINENO" .
test_special_builtin_assign_i   "$LINENO" .
test_special_builtin_redirect   "$LINENO" .
test_special_builtin_redirect_s "$LINENO" .
test_special_builtin_redirect_i "$LINENO" .
test_special_builtin_assign     "$LINENO" break
test_special_builtin_assign_s   "$LINENO" break
test_special_builtin_assign_i   "$LINENO" break
test_special_builtin_redirect   "$LINENO" break
test_special_builtin_redirect_s "$LINENO" break
test_special_builtin_redirect_i "$LINENO" break
test_special_builtin_assign     "$LINENO" continue
test_special_builtin_assign_s   "$LINENO" continue
test_special_builtin_assign_i   "$LINENO" continue
test_special_builtin_redirect   "$LINENO" continue
test_special_builtin_redirect_s "$LINENO" continue
test_special_builtin_redirect_i "$LINENO" continue
test_special_builtin_assign     "$LINENO" eval
test_special_builtin_assign_s   "$LINENO" eval
test_special_builtin_assign_i   "$LINENO" eval
test_special_builtin_redirect   "$LINENO" eval
test_special_builtin_redirect_s "$LINENO" eval
test_special_builtin_redirect_i "$LINENO" eval
test_special_builtin_assign     "$LINENO" exec
test_special_builtin_assign_s   "$LINENO" exec
test_special_builtin_assign_i   "$LINENO" exec
test_special_builtin_redirect   "$LINENO" exec
test_special_builtin_redirect_s "$LINENO" exec
test_special_builtin_redirect_i "$LINENO" exec
test_special_builtin_assign     "$LINENO" exit
test_special_builtin_assign_s   "$LINENO" exit
test_special_builtin_assign_i   "$LINENO" exit
test_special_builtin_redirect   "$LINENO" exit
test_special_builtin_redirect_s "$LINENO" exit
test_special_builtin_redirect_i "$LINENO" exit
test_special_builtin_assign     "$LINENO" export
test_special_builtin_assign_s   "$LINENO" export
test_special_builtin_assign_i   "$LINENO" export
test_special_builtin_redirect   "$LINENO" export
test_special_builtin_redirect_s "$LINENO" export
test_special_builtin_redirect_i "$LINENO" export
test_special_builtin_assign     "$LINENO" readonly
test_special_builtin_assign_s   "$LINENO" readonly
test_special_builtin_assign_i   "$LINENO" readonly
test_special_builtin_redirect   "$LINENO" readonly
test_special_builtin_redirect_s "$LINENO" readonly
test_special_builtin_redirect_i "$LINENO" readonly
test_special_builtin_assign     "$LINENO" return
test_special_builtin_assign_s   "$LINENO" return
test_special_builtin_assign_i   "$LINENO" return
test_special_builtin_redirect   "$LINENO" return
test_special_builtin_redirect_s "$LINENO" return
test_special_builtin_redirect_i "$LINENO" return
test_special_builtin_assign     "$LINENO" set
test_special_builtin_assign_s   "$LINENO" set
test_special_builtin_assign_i   "$LINENO" set
test_special_builtin_redirect   "$LINENO" set
test_special_builtin_redirect_s "$LINENO" set
test_special_builtin_redirect_i "$LINENO" set
test_special_builtin_assign     "$LINENO" shift
test_special_builtin_assign_s   "$LINENO" shift
test_special_builtin_assign_i   "$LINENO" shift
test_special_builtin_redirect   "$LINENO" shift
test_special_builtin_redirect_s "$LINENO" shift
test_special_builtin_redirect_i "$LINENO" shift
test_special_builtin_assign     "$LINENO" times
test_special_builtin_assign_s   "$LINENO" times
test_special_builtin_assign_i   "$LINENO" times
test_special_builtin_redirect   "$LINENO" times
test_special_builtin_redirect_s "$LINENO" times
test_special_builtin_redirect_i "$LINENO" times
test_special_builtin_assign     "$LINENO" trap
test_special_builtin_assign_s   "$LINENO" trap
test_special_builtin_assign_i   "$LINENO" trap
test_special_builtin_redirect   "$LINENO" trap
test_special_builtin_redirect_s "$LINENO" trap
test_special_builtin_redirect_i "$LINENO" trap
test_special_builtin_assign     "$LINENO" unset
test_special_builtin_assign_s   "$LINENO" unset
test_special_builtin_assign_i   "$LINENO" unset
test_special_builtin_redirect   "$LINENO" unset
test_special_builtin_redirect_s "$LINENO" unset
test_special_builtin_redirect_i "$LINENO" unset

# Command syntax error for special built-ins is not tested here because we can
# not portably cause syntax error since any syntax can be accepted as an
# extension.

# vim: set ft=sh ts=8 sts=4 sw=4 noet:

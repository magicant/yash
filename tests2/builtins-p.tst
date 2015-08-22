# builtins-p.tst: test of built-ins' attributes for any POSIX-compliant shell

posix="true"

##### Special built-ins

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

test_o 'assignment on special built-in colon is persistent'
a=a
a=b :
echo $a
__IN__
b
__OUT__

test_o 'assignment on special built-in dot is persistent'
a=a
a=b . /dev/null
echo $a
__IN__
b
__OUT__

test_o 'assignment on special built-in break is persistent'
a=a
for i in 1; do
    a=b break
done
echo $a
__IN__
b
__OUT__

test_o 'assignment on special built-in continue is persistent'
a=a
for i in 1; do
    a=b continue
done
echo $a
__IN__
b
__OUT__

test_o 'assignment on special built-in eval is persistent'
a=a
a=b eval ''
echo $a
__IN__
b
__OUT__

test_o 'assignment on special built-in exec is persistent'
a=a
a=b exec
echo $a
__IN__
b
__OUT__

#test_o 'assignment on special built-in exit is persistent'

test_o 'assignment on special built-in export is persistent'
a=a
a=b export c=c
echo $a
__IN__
b
__OUT__

test_o 'assignment on special built-in readonly is persistent'
a=a
a=b readonly c=c
echo $a
__IN__
b
__OUT__

test_o 'assignment on special built-in return is persistent'
f() { a=b return; }
a=a
f
echo $a
__IN__
b
__OUT__

test_o 'assignment on special built-in set is persistent'
a=a
a=b set ''
echo $a
__IN__
b
__OUT__

test_o 'assignment on special built-in shift is persistent'
a=a
a=b shift 0
echo $a
__IN__
b
__OUT__

test_o 'assignment on special built-in times is persistent'
a=a
a=b times >/dev/null
echo $a
__IN__
b
__OUT__

test_o 'assignment on special built-in trap is persistent'
a=a
a=b trap - TERM
echo $a
__IN__
b
__OUT__

test_o 'assignment on special built-in unset is persistent'
a=a
a=b unset b
echo $a
__IN__
b
__OUT__

test_O 'function cannot override special built-in colon'
:() { echo not reached; }
:
__IN__

test_O 'function cannot override special built-in dot'
.() { echo not reached; }
. /dev/null
__IN__

test_OE 'function cannot override special built-in break'
break() { echo not reached; }
for i in 1; do
    break
done
__IN__

test_OE 'function cannot override special built-in continue'
continue() { echo not reached; }
for i in 1; do
    continue
done
__IN__

test_OE 'function cannot override special built-in eval'
eval() { echo not reached; }
eval ''
__IN__

test_OE 'function cannot override special built-in exec'
exec() { echo not reached; }
exec
__IN__

test_OE 'function cannot override special built-in exit'
exit() { echo not reached; }
exit
__IN__

test_OE 'function cannot override special built-in export'
export() { echo not reached; }
export a=a
__IN__

test_OE 'function cannot override special built-in readonly'
readonly() { echo not reached; }
readonly a=a
__IN__

test_OE 'function cannot override special built-in return'
return() { echo not reached; }
fn() { return; }
fn
__IN__

test_OE 'function cannot override special built-in set'
set() { echo not reached; }
set ''
__IN__

test_OE 'function cannot override special built-in shift'
shift() { echo not reached; }
shift 0
__IN__

test_E 'function cannot override special built-in times'
times() { echo not reached >&2; }
times
__IN__

test_OE 'function cannot override special built-in trap'
trap() { echo not reached; }
trap - TERM
__IN__

test_OE 'function cannot override special built-in unset'
unset() { echo not reached; }
unset unset
__IN__

(
setup 'PATH=; unset PATH'

test_OE -e 0 'special built-in colon can be invoked without $PATH'
:
__IN__

test_OE -e 0 'special built-in dot can be invoked without $PATH'
. /dev/null
__IN__

test_OE -e 0 'special built-in break can be invoked without $PATH'
for i in 1; do
    break
done
__IN__

test_OE -e 0 'special built-in continue can be invoked without $PATH'
for i in 1; do
    continue
done
__IN__

test_OE -e 0 'special built-in eval can be invoked without $PATH'
eval ''
__IN__

test_OE -e 0 'special built-in exec can be invoked without $PATH'
exec
__IN__

test_OE -e 0 'special built-in exit can be invoked without $PATH'
exit
__IN__

test_OE -e 0 'special built-in export can be invoked without $PATH'
export a=a
__IN__

test_OE -e 0 'special built-in readonly can be invoked without $PATH'
readonly a=a
__IN__

test_OE -e 0 'special built-in return can be invoked without $PATH'
fn() { return; }
fn
__IN__

test_OE -e 0 'special built-in set can be invoked without $PATH'
set ''
__IN__

test_OE -e 0 'special built-in shift can be invoked without $PATH'
shift 0
__IN__

test_E -e 0 'special built-in times can be invoked without $PATH'
times
__IN__

test_OE -e 0 'special built-in trap can be invoked without $PATH'
trap - TERM
__IN__

test_OE -e 0 'special built-in unset can be invoked without $PATH'
unset unset
__IN__

)

##### Intrinsic built-ins

(
setup 'PATH=; unset PATH'

test_OE -e 0 'intrinsic built-in alias can be invoked without $PATH'
alias a=a
__IN__

# Test of the bg built-in requires job control.
#test_OE -e 0 'intrinsic built-in bg can be invoked without $PATH'

test_OE -e 0 'intrinsic built-in cd can be invoked without $PATH'
cd .
__IN__

test_OE -e 0 'intrinsic built-in command can be invoked without $PATH'
command :
__IN__

test_OE 'intrinsic built-in false can be invoked without $PATH'
false
__IN__

#TODO: test_OE -e 0 'intrinsic built-in fc can be invoked without $PATH'

# Test of the fg built-in requires job control.
#test_OE -e 0 'intrinsic built-in fg can be invoked without $PATH'

test_OE -e 0 'intrinsic built-in getopts can be invoked without $PATH'
getopts o o -o
__IN__

test_OE -e 0 'intrinsic built-in jobs can be invoked without $PATH'
jobs
__IN__

test_OE -e 0 'intrinsic built-in kill can be invoked without $PATH'
kill -0 $$
__IN__

# Many shells including yash does not implement newgrp as a built-in.
#TODO: test_OE -e 0 'intrinsic built-in newgrp can be invoked without $PATH'

test_E -e 0 'intrinsic built-in pwd can be invoked without $PATH'
pwd
__IN__

test_OE -e 0 'intrinsic built-in read can be invoked without $PATH'
read a
_this_line_is_read_by_the_read_built_in_
__IN__

test_OE -e 0 'intrinsic built-in true can be invoked without $PATH'
true
__IN__

test_OE -e 0 'intrinsic built-in umask can be invoked without $PATH'
umask 000
__IN__

test_OE -e 0 'intrinsic built-in unalias can be invoked without $PATH'
unalias -a
__IN__

test_OE -e 0 'intrinsic built-in wait can be invoked without $PATH'
wait
__IN__

)

# vim: set ft=sh ts=8 sts=4 sw=4 noet:

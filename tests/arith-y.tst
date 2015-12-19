# arith-y.tst: yash-specific test of arithmetic expansion

test_oE 'prefix ++'
x=1
echo $((++x))
echo $((++x))
echo $((++x))
echo $x
__IN__
2
3
4
4
__OUT__

test_oE 'prefix --'
x=1
echo $((--x))
echo $((--x))
echo $((--x))
echo $x
__IN__
0
-1
-2
-2
__OUT__

test_oE 'postfix ++'
x=1
echo $((x++))
echo $((x++))
echo $((x++))
echo $x
__IN__
1
2
3
4
__OUT__

test_oE 'postfix --'
x=1
echo $((x--))
echo $((x--))
echo $((x--))
echo $x
__IN__
1
0
-1
-2
__OUT__

# $1 = line no.
# $2 = arithmetic expression that causes division by zero
test_division_by_zero() {
    testcase "$1" -e 2 "division by zero ($2)" 3<<__IN__ 4</dev/null 5<<\__ERR__
eval 'echo \$(($2))'
__IN__
eval: arithmetic: division by zero
__ERR__
}

test_division_by_zero "$LINENO" '0/0'
test_division_by_zero "$LINENO" '0%0'
test_division_by_zero "$LINENO" '1/0'
test_division_by_zero "$LINENO" '1%0'
(
setup 'x=0'
test_division_by_zero "$LINENO" 'x/=0'
test_division_by_zero "$LINENO" 'x%=0'
)
(
setup 'x=1'
test_division_by_zero "$LINENO" 'x/=0'
test_division_by_zero "$LINENO" 'x%=0'
)

# TODO: tests for floating-point arithmetics are missing

# vim: set ft=sh ts=8 sts=4 sw=4 noet:

# shift-y.tst: yash-specific test of the shift built-in

setup -d

test_o 'positional parameters are not modified on error' -s a 'b  b' c
shift 4
bracket "$#" "$@"
__IN__
[3][a][b  b][c]
__OUT__

test_Oe -e 2 'too many operands'
shift 1 2
__IN__
shift: too many operands are specified
__ERR__

test_Oe -e 2 'invalid option -z'
shift -z
__IN__
shift: `-z' is not a valid option
__ERR__
#'
#`

test_Oe -e 2 'invalid option --xxx'
shift --no-such=option
__IN__
shift: `--no-such=option' is not a valid option
__ERR__
#'
#`

test_O -d -e 2 'invalid operand (non-numeric)'
shift a
__IN__

test_O -d -e 2 'invalid operand (non-integral)' -s 1
shift 1.0
__IN__

test_Oe -e 2 'invalid operand (negative)'
shift -- -1
__IN__
shift: -1: the operand value must not be negative
__ERR__

# vim: set ft=sh ts=8 sts=4 sw=4 noet:

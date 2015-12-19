# suspend-y.tst: yash-specific test of the suspend built-in

test_Oe -e 2 'too many operands'
suspend foo
__IN__
suspend: no operand is expected
__ERR__

test_Oe -e 2 'invalid option --xxx'
suspend --no-such=option
__IN__
suspend: `--no-such=option' is not a valid option
__ERR__
#'
#`

# vim: set ft=sh ts=8 sts=4 sw=4 noet:

# wait-y.tst: yash-specific test of the wait built-in

test_Oe -e 2 'invalid option --xxx'
wait --no-such=option
__IN__
wait: `--no-such=option' is not a valid option
__ERR__
#'
#`

# vim: set ft=sh ts=8 sts=4 sw=4 noet:

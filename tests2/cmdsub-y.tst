# cmdsub-y.tst: yash-specific test of command substitution

test_oE 'disambiguation with arithmetic expansion'
echo $((echo foo); (echo bar))
__IN__
foo bar
__OUT__

# vim: set ft=sh ts=8 sts=4 sw=4 noet:

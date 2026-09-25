# true-y.tst: yash-specific test of the true built-in

test_OE -e 0 'true ignores arguments'
true foo -x --
__IN__

# vim: set ft=sh ts=8 sts=4 sw=4 et:

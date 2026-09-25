# false-y.tst: yash-specific test of the false built-in

test_OE -e n 'false ignores arguments'
false foo -x --
__IN__

# vim: set ft=sh ts=8 sts=4 sw=4 et:

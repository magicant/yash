# disown-y.tst: yash-specific test of the disown built-in

test_oE -e 0 'disown is a semi-special built-in'
command -V disown
__IN__
disown: a semi-special built-in
__OUT__

# vim: set ft=sh ts=8 sts=4 sw=4 noet:

# disown-y.tst: yash-specific test of the disown built-in

test_OE -e 0 'omitting % in job ID'
sleep 1&
disown sleep
__IN__

test_oE -e 0 'disown is a semi-special built-in'
command -V disown
__IN__
disown: a semi-special built-in
__OUT__

(
posix="true"

test_Oe -e 1 'initial % cannot be omitted in POSIX mode'
disown foo
__IN__
disown: `foo' is not a valid job specification
__ERR__
#'
#`

)

# vim: set ft=sh ts=8 sts=4 sw=4 noet:

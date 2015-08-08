# exit-p.tst: test of the exit built-in for any POSIX-compliant shell

posix="true"

test_OE -e 0 'exiting with 0'
false
exit 0
__IN__

test_OE -e 17 'exiting with 17'
exit 17
__IN__

test_OE -e 19 'exiting with 19 in subshell'
(exit 19)
__IN__

test_OE -e 0 'default exit status without previous command'
exit
__IN__

test_OE -e 0 'default exit status with previous succeeding command'
true
exit
__IN__

test_OE -e 5 'default exit status with previous failing command'
(exit 5)
exit
__IN__

test_OE -e 3 'default exit status in subshell'
(exit 3)
(exit)
__IN__

test_oE -e 19 'exiting with trap'
trap 'echo TRAP' EXIT
exit 19
__IN__
TRAP
__OUT__

test_OE -e 1 'exit status with trap'
trap '(exit 2)' EXIT
(exit 1)
exit
__IN__

test_OE -e 0 'exiting from trap with 0'
trap 'exit 0' EXIT
exit 1
__IN__

test_OE -e 7 'exiting from trap with 7'
trap 'exit 7' EXIT
exit 1
__IN__

test_OE -e 2 'default exit status in trap in exiting with default'
trap exit EXIT
(exit 2)
exit
__IN__

test_OE -e 2 \
    'default exit status with previous command in trap in exiting with default'
trap '(exit 1); exit' EXIT
(exit 2)
exit
__IN__

# POSIX says the exit status in this case should be that of "the command that
# executed immediately preceding the trap action." Many shells including yash
# interprets it as the exit status of "exit" rather than "trap."
test_OE -e 1 'default exit status in trap in exiting with 1'
trap exit EXIT
exit 1
__IN__

# vim: set ft=sh ts=8 sts=4 sw=4 noet:

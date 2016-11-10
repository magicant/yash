# readonly-p.tst: test of the readonly built-in for any POSIX-compliant shell

posix="true"

test_o -d -e n 'making one variable read-only'
readonly a=bar
echo $a
a=X && test "$a" = bar
__IN__
bar
__OUT__

test_o -d -e n 'making many variables read-only'
a=X b=B c=X
readonly a=A b c=C
echo $a $b $c
a=X || b=Y || c=Z && test "$a/$b/$c" = A/B/C
__IN__
A B C
__OUT__

# This test is in readonly-y.tst because it fails on some existing shells
# because of pre-defined read-only variables.
#test_x 'reusing printed read-only variables'

test_O -d -e n 'read-only variable cannot be re-assigned'
readonly a=1
readonly a=2
echo not reached # special built-in error kills non-interactive shell
__IN__

# vim: set ft=sh ts=8 sts=4 sw=4 noet:

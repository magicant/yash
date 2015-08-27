# invoke-y.tst: yash-specific test of shell invocation

##### -c and -s combinations

testcase "$LINENO" -e 2 'missing command with -c' -c \
	3</dev/null 4</dev/null 5<<__ERR__
$testee: the -c option is specified but no command is given
__ERR__

testcase "$LINENO" -e 2 'specifying both -c and -s (separate)' -c -s \
	3</dev/null 4</dev/null 5<<__ERR__
$testee: the -c option cannot be used with the -s option
__ERR__

testcase "$LINENO" -e 2 'specifying both -c and -s (combined)' -cs \
	3</dev/null 4</dev/null 5<<__ERR__
$testee: the -c option cannot be used with the -s option
__ERR__

testcase "$LINENO" -e 2 'specifying both -c and -s (long)' --cmdlin --stdi \
	3</dev/null 4</dev/null 5<<__ERR__
$testee: the -c option cannot be used with the -s option
__ERR__

testcase "$LINENO" -e 2 'specifying both -c and -s (-o)' -o cmdlin -o stdi \
	3</dev/null 4</dev/null 5<<__ERR__
$testee: the -c option cannot be used with the -s option
__ERR__

test_oE -e 0 'negating -c and enabling -s' -c +c -s
echo ok
__IN__
ok
__OUT__

testcase "$LINENO" -e 2 'unexpected option argument' --norc=_unexpected_ \
	3</dev/null 4</dev/null 5<<__ERR__
$testee: --norc=_unexpected_: the --norcfile option does not take an argument
__ERR__

testcase "$LINENO" -e 2 'missing profile option argument' --profile \
	3</dev/null 4</dev/null 5<<__ERR__
$testee: the --profile option requires an argument
__ERR__

testcase "$LINENO" -e 2 'missing rcfile option argument' --rcfile \
	3</dev/null 4</dev/null 5<<__ERR__
$testee: the --rcfile option requires an argument
__ERR__

test_O -d -e 2 'ambiguous option' --p
__IN__

# vim: set ft=sh ts=8 sts=4 sw=4 noet:

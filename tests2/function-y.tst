# function-y.tst: yash-specific test of functions

(
posix="true"

test_Oe -e 2 'function keyword (-o posix)'
function foo() { echo foo; }
__IN__
syntax error: `function' cannot be used as a command name
__ERR__
#'
#`

# XXX Maybe the error message could be improved.
test_Oe -e 2 'invalid character - in function name'
a-b() { echo foo; }
__IN__
syntax error: invalid use of `('
__ERR__
#'
#`

)

test_oE 'function definition w/ function keyword w/ parentheses, grouping'
function foo(){ echo bar; }
echo $?
foo
__IN__
0
bar
__OUT__

test_oE 'function definition w/ function keyword w/o parentheses, grouping'
function foo { echo bar; }
echo $?
foo
__IN__
0
bar
__OUT__

test_oE 'function definition w/ function keyword w/ parentheses, subshell'
function foo()(echo bar)
echo $?
foo
__IN__
0
bar
__OUT__

test_oE 'function definition w/ function keyword w/o parentheses, subshell, single line'
function foo(echo bar)
echo $?
foo
__IN__
0
bar
__OUT__

test_oE 'function definition w/ function keyword w/o parentheses, subshell, multi-line'
function foo(
    echo bar)
echo $?
foo
__IN__
0
bar
__OUT__

test_oE 'function definition w/ function keyword w/o parentheses, subshell, single line'
function foo(echo bar)
echo $?
foo
__IN__
0
bar
__OUT__

test_oE 'function definition with whitespaces in between'
function 	 foo	(  )	{ echo bar; }
echo $?
foo
__IN__
0
bar
__OUT__

test_oE 'function definition w/ function keyword w/ parentheses, linebreak'
function 	 foo	(  )

{ echo bar; }
echo $?
foo
__IN__
0
bar
__OUT__

test_oE 'function definition w/ function keyword w/o parentheses, linebreak'
function 	 foo

{ echo bar; }
echo $?
foo
__IN__
0
bar
__OUT__

test_oE 'quotes and expansions in function name'
HOME=/ G=g
function ~/a/b"c\$d"e$(echo f)$((1+1))${G}h { echo foo; }
command -f '//a/bc$def2gh'
__IN__
foo
__OUT__

# vim: set ft=sh ts=8 sts=4 sw=4 noet:

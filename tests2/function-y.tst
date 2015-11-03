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

test_Oe -e 2 'simple command as function body'
foo() echo >/dev/null
__IN__
syntax error: a function body must be a compound command
__ERR__
#'
#`

test_Oe -e 2 'function definition as function body'
foo() bar() { :; }
__IN__
syntax error: a function body must be a compound command
syntax error: invalid use of `('
__ERR__
#'
#`

test_Oe -e 2 'function followed by EOF'
function
__IN__
syntax error: a word is required after `function'
syntax error: a function body must be a compound command
__ERR__
#'
#`

test_Oe -e 2 'function followed by symbol'
function |
__IN__
syntax error: a word is required after `function'
syntax error: a function body must be a compound command
syntax error: a command is missing at the end of input
__ERR__
#'
#`

test_Oe -e 2 'function followed by newline'
function
foo() { :; }
__IN__
syntax error: a word is required after `function'
syntax error: a function body must be a compound command
__ERR__
#'
#`

test_oE 'function as function name'
function function () {
    echo foo
}
\function
__IN__
foo
__OUT__

test_Oe -e 2 'function name followed by EOF (w/ function keyword)'
function foo
__IN__
syntax error: a function body must be a compound command
__ERR__

test_Oe -e 2 'parentheses followed by EOF (w/ function keyword)'
function foo()
__IN__
syntax error: a function body must be a compound command
__ERR__

test_Oe -e 2 'parentheses followed by EOF (w/o function keyword)'
foo()
__IN__
syntax error: a function body must be a compound command
__ERR__

test_Oe -e 2 'unpaired parenthesis (w/o function keyword)'
foo(
__IN__
syntax error: `(' must be followed by `)' in a function definition
__ERR__
#'
#`
#'
#`

# vim: set ft=sh ts=8 sts=4 sw=4 noet:

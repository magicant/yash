# quote-y.tst: yash-specific test of quoting

test_oE 'backslash preceding EOF is ignored'
"$TESTEE" -c 'printf "[%s]\n" 123\'
__IN__
[123]
__OUT__

test_oE 'line continuation in function definition'
\
f\
u\
n\
c\
t\
i\
o\
\
n\
	\
f"u\
n"c \
(\
 )\
\
{ echo foo; }
func
__IN__
foo
__OUT__

test_oE 'line continuation in parameter expansion'
f=foo
# echo ${#?} ${${f}} ${f[1,2]:+x}
echo \
$\
{\
\
#\
\
?\
\
} $\
\
{\
\
$\
\
{\
\
f\
\
}\
\
} $\
{\
f\
\
[\
\
1\
\
,\
\
2\
\
]\
\
:\
\
+\
\
x\
\
}
__IN__
1 foo x
__OUT__

test_Oe -e 2 'unclosed single quotation'
echo 'foo
-
__IN__
syntax error: the single quotation is not closed
__ERR__
#'

test_Oe -e 2 'unclosed double quotation (direct)'
echo "foo
__IN__
syntax error: the double quotation is not closed
__ERR__
#"

test_Oe -e 2 'unclosed double quotation (in parameter expansion)'
echo ${foo-"bar}
__IN__
syntax error: the double quotation is not closed
syntax error: `}' is missing
__ERR__
#'
#`
#"

# vim: set ft=sh ts=8 sts=4 sw=4 noet:

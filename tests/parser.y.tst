# parser.y.tst: yash-specific test of syntax parser
# vim: set ft=sh ts=8 sts=4 sw=4 noet:

echo ===== for =====

# semicolon after identifier only allowed in non-posix mode
set 1 2 3
for i; do echo $i; done
for i;
do echo $i;
done

echo ===== empty compound commands =====

(exit 1)
()
(
)
{ }
{
}
echo a $?

(exit 1)
if then fi
if
then
fi
if then elif then else fi
if
then
elif
then
else
fi
echo b $?

(exit 1)
for _ in; do done
echo c1 $?
(exit 1)
for _ in
do
done
echo c2 $?
(exit 1)
for _ in 1 2 3; do done
echo c3 $?

while false; do done
echo d1 $?
false
until do done
echo d2 $?
i=3
while do
    if [ $i -eq 0 ]; then break; fi
    i=$((i-1))
    echo $i
done
until do
    echo not reached
done

false
case _ in
    (*)
esac
echo e1 $?

echo ===== function =====

func ()
function  func  (   ) { echo func 1; }
func
func

function func()
{ echo func 2; }
func

function func
if true
then echo func 3
else echo X 3
fi
func

function "func" (echo func 4)
func

funcname=Z
func ()
function X"Y$(echo $funcname)"
for i in func
do
    unset -f $i
    echo func 5
done
func
XYZ

typeset -fp

function X:Z
{
    echo non-portable name
}

typeset -fp X:Z

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
 )


{ echo func 6; }
func

echo ===== error =====

$INVOKE $TESTEE <<\END
if true; then echo error missing semicolon 1; fi echo error missing semicolon 2
echo error missing semicolon 3
END
$INVOKE $TESTEE <<\END
)
echo error right paren
END
$INVOKE $TESTEE <<\END
}
echo error right brace
END
$INVOKE $TESTEE <<\END
;;
echo error double semicolon
END
$INVOKE $TESTEE <<\END
then
echo error then 1
END
$INVOKE $TESTEE <<\END
if true; then :; then
echo error then 2
END
$INVOKE $TESTEE <<\END
else
echo error else 1
END
$INVOKE $TESTEE <<\END
if true; else
echo error else 2
END
$INVOKE $TESTEE <<\END
elif
echo error elif 1
END
$INVOKE $TESTEE <<\END
if true; elif
echo error elif 2
END
$INVOKE $TESTEE <<\END
fi
echo error fi 1
END
$INVOKE $TESTEE <<\END
if true; fi
echo error fi 2
END
$INVOKE $TESTEE <<\END
if true; then :; elif true; fi
echo error fi 3
END
$INVOKE $TESTEE <<\END
do
echo error do
END
$INVOKE $TESTEE <<\END
done
echo error done 1
END
$INVOKE $TESTEE <<\END
while false; done
echo error done 2
END
$INVOKE $TESTEE <<\END
esac
echo error esac
END

$INVOKE $TESTEE <<\END
! ! echo error ! 1
echo error ! 2
END
$INVOKE $TESTEE <<\END
: | : | ! echo error ! 3
echo error ! 4
END
$INVOKE $TESTEE <<\END
in
echo error in
END

$INVOKE $TESTEE <<\END
echo no command after pipe |
END
$INVOKE $TESTEE -c '; echo no command before semicolon'
$INVOKE $TESTEE -c '& echo no command before ampersand'
$INVOKE $TESTEE -c '| echo no command before pipe'
$INVOKE $TESTEE -c '(;)'
$INVOKE $TESTEE -c '(&)'
$INVOKE $TESTEE -c '(| echo no command before pipe)'
$INVOKE $TESTEE -c 'echo \
invalid left parenthesis \
('
$INVOKE $TESTEE -c '\foo () { echo invalid left parenthesis; }'
$INVOKE $TESTEE -c 'array=(
1 2 3
'
$INVOKE $TESTEE -c 'array=( foo ;)'

$INVOKE $TESTEE -c '<'
$INVOKE $TESTEE -c '<>'
$INVOKE $TESTEE -c '<&#'
$INVOKE $TESTEE -c '>'
$INVOKE $TESTEE -c '>|'
$INVOKE $TESTEE -c '>&'
$INVOKE $TESTEE -c '>> \
#'
$INVOKE $TESTEE -c '>>|'
$INVOKE $TESTEE <<\END
cat <<
END
$INVOKE $TESTEE <<\END
cat <<- \

END
$INVOKE $TESTEE <<\END
cat <<<
END

$INVOKE $TESTEE -c 'echo "foo'
$INVOKE $TESTEE -c 'echo ${foo-"bar}'
$INVOKE $TESTEE -c "echo \\
'foo
"

$INVOKE $TESTEE -c 'echo ${}'
$INVOKE $TESTEE -c 'echo ${}}'
$INVOKE $TESTEE -c 'echo ${&}'
$INVOKE $TESTEE -c 'echo ${foo[]}'
$INVOKE $TESTEE -c 'echo ${foo[,2]}'
$INVOKE $TESTEE -c 'echo ${foo[2,]}'
$INVOKE $TESTEE -c 'echo ${foo[}'
$INVOKE $TESTEE -c 'echo ${foo[1}'
$INVOKE $TESTEE -c 'echo ${foo[1,2}'
$INVOKE $TESTEE -c 'echo ${foo'
$INVOKE $TESTEE -c 'echo ${foo:}'
$INVOKE $TESTEE -c 'echo ${foo:'
$INVOKE $TESTEE -c 'echo ${foo!'
$INVOKE $TESTEE -c 'echo ${foo:#bar}'
$INVOKE $TESTEE -c 'echo ${foo:%bar}'
$INVOKE $TESTEE -c 'echo ${foo#bar'
$INVOKE $TESTEE -c 'echo ${foo%bar'
$INVOKE $TESTEE -c 'echo ${foo/bar'
$INVOKE $TESTEE -c 'echo ${foo/bar/baz'
$INVOKE $TESTEE -c 'echo ${#foo-bar}'
$INVOKE $TESTEE -c 'echo ${#foo+bar}'
$INVOKE $TESTEE -c 'echo ${#foo?bar}'
$INVOKE $TESTEE -c 'echo ${#foo=bar}'
$INVOKE $TESTEE -c 'echo ${#foo#bar}'
$INVOKE $TESTEE -c 'echo ${#foo%bar}'
$INVOKE $TESTEE -c 'echo ${#foo/bar}'
$INVOKE $TESTEE -c 'echo ${#foo/bar/baz}'

$INVOKE $TESTEE -c 'echo $(echo missing right parenthesis
'
$INVOKE $TESTEE -c 'echo `echo missing backquote
'
$INVOKE $TESTEE -c 'echo `echo missing backquote \`
'
$INVOKE $TESTEE -c 'echo missing double-quote `echo \
"foo`'
$INVOKE $TESTEE -c 'echo missing backquote \
`echo foo \`
echo bar`'
$INVOKE $TESTEE -c 'echo $((echo missing right parenthesis'
echo = >&2
$INVOKE $TESTEE -c 'echo $((echo missing right parenthesis)'

$INVOKE $TESTEE --posix <<\END
( )
END
$INVOKE $TESTEE --posix <<\END
(
    # comment
)
END
$INVOKE $TESTEE --posix <<\END
{ }
END
$INVOKE $TESTEE --posix <<\END
{
    # comment
}
END
$INVOKE $TESTEE -c '('
$INVOKE $TESTEE -c '{'
$INVOKE $TESTEE -c '{ ( }'
$INVOKE $TESTEE -c '( { )'

$INVOKE $TESTEE --posix <<\END
if    then :; elif :; then :; else :; fi
END
$INVOKE $TESTEE --posix <<\END
if :; then    elif :; then :; else :; fi
END
$INVOKE $TESTEE --posix <<\END
if :; then :; elif    then :; else :; fi
END
$INVOKE $TESTEE --posix <<\END
if :; then :; elif :; then    else :; fi
END
$INVOKE $TESTEE --posix <<\END
if :; then :; elif :; then :; else    fi
END
$INVOKE $TESTEE -c 'if'
$INVOKE $TESTEE -c 'if :; then echo then missing; elif'
$INVOKE $TESTEE -c 'if :; then echo fi missing'
$INVOKE $TESTEE -c 'if :; then :; else echo fi missing'
$INVOKE $TESTEE -c '{ if }'
$INVOKE $TESTEE -c '{ if :; then echo then missing; elif }'
$INVOKE $TESTEE -c '{ if :; then echo fi missing; }'
$INVOKE $TESTEE -c '{ if :; then :; else echo fi missing; }'

$INVOKE $TESTEE <<\END
for
do
    echo missing variable
done
END
$INVOKE $TESTEE -c 'for = do echo invalid variable; done'
$INVOKE $TESTEE -c 'for _ in 1 2 3>/dev/null; do echo redirection in for; done'
$INVOKE $TESTEE --posix -c 'for _; do echo invalid semicolon; done'
$INVOKE $TESTEE -c 'for _ echo do missing; done'
$INVOKE $TESTEE --posix <<\END
for _ do done
END
$INVOKE $TESTEE --posix <<\END
for _
do
    # comment
done
END
$INVOKE $TESTEE -c 'for _ do'
$INVOKE $TESTEE -c '{ for }'
$INVOKE $TESTEE -c '{ for _ }'
$INVOKE $TESTEE -c '{ for _ in }'
$INVOKE $TESTEE -c '{ for _ in; }'
$INVOKE $TESTEE -c '{ for _ do }'
$INVOKE $TESTEE -c '{ for _ in; do }'

$INVOKE $TESTEE --posix <<\END
while do :; done
END
$INVOKE $TESTEE --posix <<\END
until do :; done
END
$INVOKE $TESTEE --posix <<\END
while :; do done
END
$INVOKE $TESTEE --posix <<\END
until :; do done
END
$INVOKE $TESTEE -c 'while'
$INVOKE $TESTEE -c 'while do'
$INVOKE $TESTEE -c 'until'
$INVOKE $TESTEE -c 'until do'
$INVOKE $TESTEE -c '{ while }'
$INVOKE $TESTEE -c '{ while do }'
$INVOKE $TESTEE -c '{ until }'
$INVOKE $TESTEE -c '{ until do }'

$INVOKE $TESTEE <<\END
case # comment
in
esac
END
$INVOKE $TESTEE -c 'case'
$INVOKE $TESTEE -c 'case ""'
$INVOKE $TESTEE -c 'case "" in'
$INVOKE $TESTEE -c 'case ; echo no word after case'
$INVOKE $TESTEE -c '{ case "" }'
$INVOKE $TESTEE -c '{ case "" in *) }'
$INVOKE $TESTEE <<\END
case "" in (
END
$INVOKE $TESTEE -c 'case "" in >/dev/null'
$INVOKE $TESTEE -c 'case "" in "" esac'
$INVOKE $TESTEE -c 'case "" in "" | "" esac'
$INVOKE $TESTEE --posix -c 'case "" in (esac) :; esac'

$INVOKE $TESTEE --posix -c 'function f() { echo posix function; }'
$INVOKE $TESTEE <<\END
function # comment
{ echo no word after function; }
END
$INVOKE $TESTEE -c 'function f() echo non-compound function body'
$INVOKE $TESTEE -c 'f ('
$INVOKE $TESTEE -c 'f() echo non-compound function body'

$INVOKE $TESTEE <<\END
cat <<'a
b'
END

# parser.p.tst: test of syntax parser for any POSIX-compliant shell
# vim: set ft=sh ts=8 sts=4 sw=4 noet:

tmp="${TESTTMP}/parser.p.tmp"

var=abc
echo 1\2\
3
echo $var ${var} "$var" "${var}" '$var' '${var}'
echo \$var \${var} "\$var" "\${var}"
echo $(echo $var ${var} "$var" "${var}" '$var' '${var}')
echo "$(echo $var ${var} "$var" "${var}" '$var' '${var}')"
echo '$(echo $var ${var} "$var" "${var}")'
echo single \
'
'
echo double \
"
"

foo= bar= multiple=assignments one=line
echo $multiple $one
multiple=foo ;
one=&
(one\
=bar)
echo $multiple $one

echo $(echo echo) $((1 + 2 * 3))

echo dummy >/dev/null

exec </dev/null
foo=$(echo foo; exit 10) >/dev/null
echo $? $foo
bar=bar >$(echo /dev/null; exit 12)
echo $? $bar
foo=$(echo $?; exit 1) <&$(echo $?; exit 2)
echo $? $foo

echo ===== 0 =====

# echo $var ${var#a} ${var%%b*}
e\
c\
h\
o\
 \
$\
v\
a\
r\
 \
$\
{\
v\
a\
r\
#\
a\
}\
 \
${var\
%\
%\
b\
*\
}

echo ===== 1 =====

echo comment# \# # comment

echo                                                                          very                                                                very                                          lo\
ng                                                                                                  line

echo pipeline \| | cat |
cat \
| cat

echo test ;echo of
echo sequential \;; echo lists ;

echo asynchronous list\
&
wait $!
(echo another async; false) &
if ! wait $!; then echo ok; fi

echo ===== 2 =====

true  && echo true  and
false && echo false and
true  || echo true  or
false || echo false or

false && echo and || echo or
true || echo or && echo and

true &&
echo and newline
false\
|\
|\
echo or newline

echo ===== 3 =====

{ echo compound list;}| cat
{
    echo another
    echo compound list\
}
} 2>&1|cat

(echo subshell|cat)|
cat
(
echo another
echo subshell\
)2>&1|cat
echo yet another subshell|(
cat;)

echo ===== 4 =====

if true; then echo if construct; fi
if # comment ok
    echo dummy
    false
then
    echo no!
else
    echo ok!
fi

if if true; then false; else true; fi then
    echo no!; elif echo dummy; echo dummy; then
    echo ok!; else echo no!; fi

if false;
then echo no!;
elif true &&! echo dummy;
then echo no!;
elif false ||! false;
then echo ok!;
fi

if if (true)
then (echo 1)
fi then echo 2
fi

# if true;then echo line concatenation ok \(if\)
# fi
i\
f\
 \
true\
;\
t\
h\
e\
n\
 \
echo line concatenation ok \(if\)
f\
i

echo ===== 5 =====

while false; do true; done; echo $?
until true; do true; done; echo $?
while [ $? = 0 ]; do (exit 10) done; echo $?
until ! [ $? = 0 ]; do (exit 10) done; echo $?
set 1 '2  2' 3
while # comment ok
    [ $# != 0 ]
do
    echo $# $1
    shift
done

w\
h\
i\
l\
e
echo line concatenation ok \(while\)
false;\
d\
o\
 echo ng
d\
o\
n\
e

echo ===== 6 =====

for i in 1 '2   2' 3; do echo $i; echo "$i"; done
for i             # comment
    in parser.p.* # comment
do                # comment
    echo $i       # comment
done              # comment

# for index in 1 '2  2' 3;do echo $index; echo "$index";done
f\
o\
r\
 \
i\
n\
d\
e\
x\
 \
i\
n\
 \
1 '2  2' 3\
;\
d\
o\
 echo $index; echo "$index";\
d\
o\
n\
e

for in in in; do echo $in; done

false
for i in; do :; done
echo $?
(exit 2)
for i in 1; do (exit 1); done
echo $?

echo ===== 7 =====

false
case 123 in esac
echo $?

case 123
    in
esac

case 123 in
    * | esac )
esac

i=''
case 123 # comment ok
    in   # comment ok
    ${i:=2}) echo ng\;;;
    (x|y|z) echo ng;;
    (dummy) ;;
    foo | 1$i*3 ) echo ok
    false
esac
if [ $? != 0 ]; then echo $i; fi

case '*' in # comment ok
    '*')
    echo '*'
    ;;
    *)
    echo ng
    ;;
esac

for i in 1 '2  2' 3 4 5; do
    case $i in
	1 | 4 )
	echo foo
	if [ $i = 1 ]; then echo one; else echo four; fi
	;;
	2 ) echo ng ;;
	(*2"  "[1-3]*) echo two;;
	(\*) echo ng;;
	(*) echo $i;;
    esac
done

# case case in false)echo ng;;(c*e)echo line concatenation ok \(case\);esac
c\
a\
s\
e\
 \
c\
a\
s\
e\
 \
i\
n\
 \
f\
a\
l\
s\
e\
)\
echo ng\
;\
;\
(\
c\
*\
e\
)\
echo line concatenation ok \(case\)\
;\
e\
s\
a\
c

echo ===== 8 =====

func () {
    echo function
}
func
func

# func2 () (echo line concatenation ok \(func\))
  f\
u\
\
n\
c\
2\
(\
)\
  # split function def.
(echo line concatenation ok \(func\))

func2

func3 () if true; then echo func3; fi
func3
func
func3
func

if $INVOKE $TESTEE -s 2>/dev/null <<END
f () f2 () { echo error; }
END
then echo 1
else echo 2
fi

funcredir () {
    echo redirected
    echo re-redirected >&3
} \
>"$tmp"

funcredir 3>&1 >/dev/null
cat "$tmp"

funcredir2 ( ) # comment
{
    echo not-redirected
}

funcredir2 >/dev/null


echo lineno=$LINENO


rm -f "$tmp"

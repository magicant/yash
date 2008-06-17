tmp=/tmp/yashtest.$$

var=abc
echo 1\2\
3
echo $var ${var} "$var" "${var}" '$var' '${var}'
echo \$var \${var} "\$var" "\${var}"
echo $(echo $var ${var} "$var" "${var}" '$var' '${var}')
echo "$(echo $var ${var} "$var" "${var}" '$var' '${var}')"
echo '$(echo $var ${var} "$var" "${var}")'

# TODO test for arithmetic expansion

echo dummy >/dev/null

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
#
# TODO test of asynchronous list (needs 'wait')

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
if
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
# TODO need 'shift'
# set 1 '2  2' 3
while
	[ $# != 0 ]
do
	echo $# $1
# 	shift
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
for i
	in parser.p.*
do
	echo $i
done
# TODO need 'readonly'
# for i in 1 2 3 4 5; do
# 	echo $i
# 	if [ $i = 3 ]; then
# 		readonly i
# 	fi
# done

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

echo ===== 7 =====

false
case 123 in esac
echo $?

case 123
	in
esac

i=''
case 123
	in
	${i:=2}) echo ng\;;;
	(x|y|z) echo ng;;
	(dummy) ;;
	foo | 1$i*3 ) echo ok
	false
esac
if [ $? != 0 ]; then echo $i; fi

case '*' in
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
  # splitted function def.
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
	echo re-redirected >&2
} \
>$tmp

funcredir >/dev/null
cat $tmp

funcredir2 () {
	echo not-redirected
}

funcredir2 >/dev/null


echo lineno=$LINENO


# TODO should be removed by EXIT trap
rm -f $tmp

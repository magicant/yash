# lineno-y.tst: yash-specific test of the LINENO variable

test_oE -e 0 'LINENO and single quote'
echo $LINENO 'foo
bar' $LINENO
echo $LINENO 'foo
bar' $LINENO
__IN__
1 foo
bar 1
3 foo
bar 3
__OUT__

test_oE -e 0 'LINENO and double quote'
echo "$LINENO foo
bar $LINENO"
echo "$LINENO foo
bar $LINENO"
__IN__
1 foo
bar 1
3 foo
bar 3
__OUT__

test_oE -e 0 'LINENO and line continuation'
echo $LINENO \
    $LINENO \
    $LINENO
echo $LINENO \
    $LINENO \
    $LINENO
__IN__
1 1 1
4 4 4
__OUT__

test_oE -e 0 'LINENO and here-document (with expansion)'
cat <<END; echo c $LINENO
a $LINENO \
b $LINENO
END
cat <<END; echo f $LINENO
d $LINENO \
e $LINENO
END
__IN__
a 1 b 1
c 1
d 5 e 5
f 5
__OUT__

test_oE -e 0 'LINENO and here-document (without expansion)'
: <<\END
foo\
bar
baz
END
echo $LINENO
__IN__
6
__OUT__

test_oE -e 0 'LINENO in backquotes'
echo `echo $LINENO
echo $LINENO`
echo `echo $LINENO
echo $LINENO`
__IN__
1 2
1 2
__OUT__

test_oE -e 0 'LINENO in function'
echo a $LINENO
f() {
    echo b $LINENO
    echo c $LINENO
}
f
f
echo d $LINENO
__IN__
a 1
b 3
c 4
b 3
c 4
d 8
__OUT__

test_oE -e 0 'LINENO and alias with newline'
alias e='echo x $LINENO
echo y $LINENO'
echo a $LINENO
e
echo b $LINENO
__IN__
a 3
x 4
y 5
b 6
__OUT__

test_oE -e 0 'LINENO in interactive shell is reset for each command line'
echo $LINENO
for i in 1; do
    echo \
	$LINENO
done
echo $LINENO
__IN__
1
4
6
__OUT__

# vim: set ft=sh ts=8 sts=4 sw=4 noet:

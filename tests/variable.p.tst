echo $LINENO
cat <<EOF
some
here
document
EOF
cat <<\END
another
here-document
END
echo $LINENO
echo line \
continuation
echo $LINENO

printlineno () {
	echo function
	echo $LINENO
}
printlineno
echo $LINENO

echo ===== 1 =====

var=var
echo $var
var='v'"a"r
echo $var
var=xxx echo $var

foo='123  456  789'
bar=-${foo}-
echo $bar
echo "$bar"

echo ===== 2 =====

func () {
	var=xyz
	echo $var
}
func
echo $var

# TODO  'set', 'unset', 'shift'...

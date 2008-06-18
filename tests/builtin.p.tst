savepath=$PATH

echo ===== : true false =====

:
echo $?
true
echo $?
! false # "false" may return any non-zero status
echo $?

# a function may override a non-special builtin
false () { true; }
if false; then echo false true; fi

# a non-regular builtin should be executed if not in PATH
PATH=
if : && true && false; then
	PATH=$savepath
	echo : true false
else
	PATH=$savepath
fi

echo ===== return break continue =====

retfunc () {
	return 3
	echo retfunc ng
}
retfunc
echo $?

retfunc () {
	(exit 4)
	return
	echo retfunc ng 2
}
retfunc
echo $?

retfunc () {
	retfunc_inner () {
		return $1
	}
	retfunc_inner 5
	retfunc_inner=$?
	echo retfunc
	return $retfunc_inner
}
retfunc
echo $?

# TODO check if "return" works fine in a sourced file

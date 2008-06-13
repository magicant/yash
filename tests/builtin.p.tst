savepath=$PATH

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

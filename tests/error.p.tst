echo ===== consequences of shell errors =====

$INVOKE $TESTEE 2>/dev/null <<\END
fi
echo not printed
END
if [ $? -ne 0 ]; then echo syntax error ok; fi

$INVOKE $TESTEE <<\END
. 2>/dev/null
echo not printed
END
if [ $? -ne 0 ]; then echo special builtin syntax error ok; fi

$INVOKE $TESTEE <<\END
getopts 2>/dev/null
if [ $? -ne 0 ]; then echo non-special builtin syntax error ok; fi
END

$INVOKE $TESTEE <<\END
exec 3>&-
{ exit >&3; } 2>/dev/null
echo not printed
END
if [ $? -ne 0 ]; then echo special builtin redirection error ok; fi

$INVOKE $TESTEE <<\END
exec 3>&-
{ cd . >&3; } 2>/dev/null
if [ $? -ne 0 ]; then echo non-special builtin redirection error ok; fi
END

$INVOKE $TESTEE <<\END
readonly ro=v
{ ro=v :; } 2>/dev/null
echo not printed
END
echo special builtin assignment error ok

$INVOKE $TESTEE <<\END
readonly ro=v
{ ro=v cd .; } 2>/dev/null
echo non-special builtin assignment error ok
END

$INVOKE $TESTEE <<\END
unset var
{ eval ${var?}; } 2>/dev/null
echo not printed
END
if [ $? -ne 0 ]; then echo special builtin expansion error ok; fi

$INVOKE $TESTEE <<\END
unset var
{ cd ${var?}; } 2>/dev/null
echo not printed
END
if [ $? -ne 0 ]; then echo non-special builtin expansion error ok; fi

$INVOKE $TESTEE 2>/dev/null <<\END
./no/such/command
END
if [ $? -eq 127 ]; then echo command not found error ok; fi


echo ===== . =====

$INVOKE $TESTEE 2>/dev/null <<\END
PATH=
. no_such_command
echo not printed
END
if [ $? -ne 0 ]; then echo . PATH not found error ok; fi
$INVOKE $TESTEE 2>/dev/null <<\END
. ./no/such/command
echo not printed
END
if [ $? -ne 0 ]; then echo . file not found error ok; fi

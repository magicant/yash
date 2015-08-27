# error-y.tst: yash-specific test of error conditions

test_O -d -e 2 'syntax error kills non-interactive shell'
fi
echo not reached
__IN__

test_O -d -e 2 'syntax error in eval kills non-interactive shell'
eval fi
echo not reached
__IN__

test_o -d 'syntax error in subshell'
(eval fi; echo not reached)
echo $?
__IN__
2
__OUT__

test_o -d 'syntax error spares interactive shell' -i +m
fi
echo $?
__IN__
0
__OUT__

test_O -d -e n 'expansion error kills non-interactive shell'
unset a
echo ${a?}
echo not reached
__IN__

test_o -d 'expansion error in subshell'
unset a
(echo ${a?}; echo not reached)
echo $?
__IN__
2
__OUT__

test_o -d 'expansion error spares interactive shell' -i +m
unset a
echo ${a?}
echo $?
__IN__
2
__OUT__

test_oe -d 'command not found'
./_no_such_command_
echo $?
__IN__
127
__OUT__

# vim: set ft=sh ts=8 sts=4 sw=4 noet:

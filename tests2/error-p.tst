# error-p.tst: test of error conditions for any POSIX-compliant shell

posix="true"

test_O -d -e n 'syntax error kills non-interactive shell'
fi
echo not reached
__IN__

test_O -d -e n 'syntax error in eval kills non-interactive shell'
eval fi
echo not reached
__IN__

test_o -d 'syntax error in subshell'
(eval fi; echo not reached)
[ $? -ne 0 ]
echo $?
__IN__
0
__OUT__

test_o -d 'syntax error spares interactive shell' -i +m
fi
echo reached
__IN__
reached
__OUT__

test_O -d -e n 'expansion error kills non-interactive shell'
unset a
echo ${a?}
echo not reached
__IN__

test_o -d 'expansion error in subshell'
unset a
(echo ${a?}; echo not reached)
[ $? -ne 0 ]
echo $?
__IN__
0
__OUT__

test_o -d 'expansion error spares interactive shell' -i +m
unset a
echo ${a?}
[ $? -ne 0 ]
echo $?
__IN__
0
__OUT__

# vim: set ft=sh ts=8 sts=4 sw=4 noet:

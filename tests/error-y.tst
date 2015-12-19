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

test_Oe -e 2 'built-in short option argument missing'
exec -a
__IN__
exec: the -a option requires an argument
__ERR__

test_Oe -e 2 'built-in long option argument missing'
exec --a
__IN__
exec: the --as option requires an argument
__ERR__

test_Oe -e 2 'built-in short option hyphen'
exec -c-
__IN__
exec: `-c-' is not a valid option
__ERR__
#`

test_Oe -e 2 'built-in invalid short option'
exec -cXaY
__IN__
exec: `-cXaY' is not a valid option
__ERR__
#`

test_Oe -e 2 'built-in invalid long option without argument'
exec --no-such-option
__IN__
exec: `--no-such-option' is not a valid option
__ERR__
#`

test_Oe -e 2 'built-in invalid long option with argument'
exec --no-such=option
__IN__
exec: `--no-such=option' is not a valid option
__ERR__
#`

test_Oe -e 2 'built-in unexpected option argument'
exec --cle=X
__IN__
exec: --cle=X: the --clear option does not take an argument
__ERR__

test_O -e 2 'ambiguous long option, exit status and standard output'
read --p X
__IN__

test_o 'ambiguous long option, standard error'
read --p X 2>&1 | head -n 1
__IN__
read: option `--p' is ambiguous
__OUT__
#`

# vim: set ft=sh ts=8 sts=4 sw=4 noet:

# tilde-y.tst: yash-specific test of tilde expansion

setup -d

test_oE -e 0 'tilde expansion with $HOME unset'
unset HOME
echoraw ~
__IN__
~
__OUT__

(
if id _no_such_user_ >/dev/null 2>&1; then
    skip="true"
fi
# The below test case should be skipped if the user "_no_such_user_" exists and
# the shell has a permission to show its home directory. However, the above
# test may fail to skip the test case if the "id" utility still don't have a
# permission to show its attributes. I assume such a special case doesn't
# actually happen.

test_oE -e 0 'tilde expansion for unknown user'
echoraw ~_no_such_user_
__IN__
~_no_such_user_
__OUT__

)

test_oE '~+'
PWD=/pwd
echoraw ~+
__IN__
/pwd
__OUT__

test_oE '~-'
OLDPWD=/old-pwd
echoraw ~-
__IN__
/old-pwd
__OUT__

(
if ! testee -c 'command -bv pushd' >/dev/null; then
    skip="true"
fi

test_oE -e 0 'tilde expansion for directory stack entry'
PWD=/pwd
unset DIRSTACK
echoraw ~+0 ~-0
DIRSTACK=(/foo /bar/baz)
echoraw ~+0 ~+1 ~+2 ~+3
echoraw ~-0 ~-1 ~-2 ~-3
__IN__
/pwd /pwd
/pwd /bar/baz /foo ~+3
/foo /bar/baz /pwd ~-3
__OUT__

)

# vim: set ft=sh ts=8 sts=4 sw=4 noet:

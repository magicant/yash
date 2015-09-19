# input-y.tst: yash-specific test of input processing

(
# Detail behavior of prompting and command history is different among shell
# implementations, so we don't test it in input-p.tst.
posix="true"

if [ "$(id -u)" -eq 0 ]; then
    skip="true"
fi

(
setup -d

test_o 'default prompt strings' -i +m
bracket "$PS1"
bracket "$PS2"
bracket "$PS4"
__IN__
[$ ]
[> ]
[+ ]
__OUT__

)

test_e 'PS1' -i +m
PS1='ps1 %'; a=A; echo >&2
PS1='${a} @'; echo >&2
PS1=''; echo >&2
__IN__
$ 
ps1 %
A @
__ERR__

test_e 'PS2' -i +m
PS2='${b} %'; \
echo >&2
b=B; \
echo >&2
true &&
echo >&2; exit
__IN__
$ > 
$  %
$ B %
__ERR__

# It is POSIXly unspecified (1) whether assignment to $PS4 affects the prompt
# for the assignment itself, and (2) how characters are quoted in xtrace. This
# test case tests yash-specific behavior.
test_e 'PS4' -x
echo 0
PS4='ps4 ${x}:' x='${y}'
echo 1; echo '2  2' 3
__IN__
+ echo 0
ps4 ${y}:PS4=ps4 ${x}: x=${y}
ps4 ${y}:echo 1
ps4 ${y}:echo 2  2 3
__ERR__

(
if ! "$TESTEE" -c 'command -bv fc history' >/dev/null; then
    skip="true"
fi

export HISTFILE=/dev/null HISTRMDUP=1

test_e 'job number expansion in PS1' -i +m
PS1='#!$'; echo >&2
echo >&2
echo >&2
echo >&2; exit
__IN__
$ 
#2$
#3$
#3$
__ERR__

test_e 'literal exclamation in PS1' -i +m
PS1='!!$'; echo >&2
echo >&2
echo >&2
echo >&2; exit
__IN__
$ 
!$
!$
!$
__ERR__

)

)

# vim: set ft=sh ts=8 sts=4 sw=4 noet:

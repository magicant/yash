# pipeline-p.tst: test of pipeline for any POSIX-compliant shell

posix="true"

test_o '2-command pipeline'
echo foo | cat
__IN__
foo
__OUT__

test_o '3-command pipeline'
printf '%s\n' foo bar | tail -n 1 | cat
__IN__
bar
__OUT__

test_o 'linebreak after |'
printf '%s\n' foo bar |
tail -n 1 | 
    
cat
__IN__
bar
__OUT__

test_OE -e 3 'exit status of pipeline is from last command'
exit 1 | exit 2 | exit 3
__IN__

test_oE 'exit status of negated pipelines'
exit 1 | exit 2 | exit 3
echo a $?
! exit 1 | exit 2 | exit 3
echo b $?
! exit 1 | exit 2 | exit 1
echo c $?
! exit 1 | exit 2 | exit 0
echo d $?
__IN__
a 3
b 0
c 0
d 1
__OUT__

test_oE 'stdin for first command & stdout for last are not modified'
cat | tail -n 1
foo
bar
__IN__
bar
__OUT__

test_Oe 'stderr is not modified'
(echo >&2) | (echo >&2)
__IN__


__ERR__

test_oE 'compound commands in pipeline'
{
    echo foo
    echo bar
} | (
    tail -n 1
    echo baz
) | if true; then
    cat
else
    :
fi
__IN__
bar
baz
__OUT__

test_OE 'redirection overrides pipeline'
echo foo >/dev/null | cat
echo foo | </dev/null cat
__IN__

# vim: set ft=sh ts=8 sts=4 sw=4 noet:

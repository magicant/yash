# command-p.tst: test of the command built-in for any POSIX-compliant shell

posix="true"
setup 'set -e'

# TODO: other test cases

test_oE -e 0 'describing alias (-v)'
alias abc='echo ABC'
command="$(command -v abc)"
unalias abc
eval "$command"
abc
__IN__
ABC
__OUT__

test_OE -e 0 'describing alias (-V)'
alias abc=xyz
d="$(command -V abc)"
case "$d" in
    (*abc*xyz*|*xyz*abc*) # expected output contains alias name and value
	;;
    (*)
	printf '%s\n' "$d" # print non-conforming result
	;;
esac
__IN__

# vim: set ft=sh ts=8 sts=4 sw=4 noet:

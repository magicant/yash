# trap-p.tst: test of the trap built-in for any POSIX-compliant shell

posix="true"

test_oE 'trap command is not affected by redirections effective when set' \
    -c 'trap "echo foo" EXIT >/dev/null'
__IN__
foo
__OUT__

# vim: set ft=sh ts=8 sts=4 sw=4 noet:

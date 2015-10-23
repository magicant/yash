# redir-p.tst: test of redirections for any POSIX-compliant shell

posix="true"

test_o 'redirection is temporary' -e
{
    cat </dev/null
    cat
} <<END
here
END
__IN__
here
__OUT__

# TODO: consequences of errors

# vim: set ft=sh ts=8 sts=4 sw=4 noet:

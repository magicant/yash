# error.y.tst: yash-specific test of error handling
# vim: set ft=sh ts=8 sts=4 sw=4 noet:

echo ===== ulimit =====
echo ===== ulimit ===== >&2

if type ulimit 2>/dev/null | grep -q '^ulimit: a regular built-in'; then
    ulimit --no-such-option 2>/dev/null
    echo ulimit no-such-option $?
    ulimit -a -f
    echo ulimit invalid option $?
    (ulimit >&- 2>/dev/null)
    echo ulimit output error $?
    ulimit xxx
    echo ulimit invalid operand $?
    ulimit 0 0
    echo ulimit too many operands 1 $?
    ulimit -a 0
    echo ulimit too many operands 2 $?
else
    cat <<\END
ulimit no-such-option 2
ulimit invalid option 2
ulimit output error 1
ulimit invalid operand 2
ulimit too many operands 1 2
ulimit too many operands 2 2
END
    cat >&2 <<\END
ulimit: the -a option cannot be used with the -f option
ulimit: `xxx' is not a valid integer
ulimit: too many operands are specified
ulimit: no operand is expected
END
fi

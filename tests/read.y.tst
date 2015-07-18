# read.y.tst: yash-specific test of the read built-in
# vim: set ft=sh ts=8 sts=4 sw=4 noet:

unset IFS

echo - variables are assigned even if EOF is encountered
unset a b c
read a b c </dev/null
echo $?
typeset -p a b c

echo - input ending without newline
printf 'A' | {
unset a
read a
echo $?
typeset -p a
}

echo - input ending with backslash - not raw mode
printf '%s' 'A\' | {
unset a
read a
echo $?
typeset -p a
}

echo - input ending with backslash - raw mode
printf '%s' 'A\' | {
unset a
read --raw-mode a
echo $?
typeset -p a
}

echo - array - single operand - single field
unset a
read -A a <<\END
A
END
echo $?
typeset -p a

echo - array - single operand - no field
unset a
read -A a <<\END

END
echo $?
typeset -p a

echo - array - many operands
unset a b c
read -A a b c <<\END
A B C
END
echo $?
typeset -p a b c

echo - array - too many fields
unset a b c
IFS=' -' read -A a b c <<\END
A B C-D E\\E\
E   
END
echo $?
typeset -p a b c

echo - array - too many variables
unset a b c d
read -A a b c d <<\END
A B
END
echo $?
typeset -p a b c d

echo - array - long option
unset a b c
read --array a b c <<\END
A B C
END
echo $?
typeset -p a b c

echo - array - set -o allexport
(
unset a b
set -a
read -A a b <<\END
A B C D
END
$INVOKE $TESTEE -u -c 'echo "[$a]" "[$b]"'
)


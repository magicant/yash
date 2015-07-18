# read.p.tst: test of the read built-in for any POSIX-compliant shell
# vim: set ft=sh ts=8 sts=4 sw=4 noet:

unset IFS

p() {
    printf "%s\n" "$*"
}

p - single operand - without IFS
unset a
read a <<\END
A
END
p $? "[${a-unset}]"

p - single operand - with IFS whitespace
unset a
read a <<\END
  A  
END
p $? "[${a-unset}]"

p - single operand - with IFS non-whitespace
unset a
read a <<\END
 - A - 
END
p $? "[${a-unset}]"

p - EOF fails read
unset a
! read a </dev/null
p $? "[${a-unset}]"

p - read do not read more than needed
{
    read a
    echo B
    cat
} <<\END
\
A
C
END

p - set -o allexport
(
unset a b
set -a
read a b <<\END
A B
END
$INVOKE $TESTEE -u -c 'echo "[$a]" "[$b]"'
)

p - line continuation - followed by normal line
unset a b
read a b <<\END
A\
A B\
B
END
p $? "[${a-unset}]" "[${b-unset}]"

p - line continuation - followed by EOF
unset a b
! read a b <<\END
A\
END
p $? "[${a-unset}]" "[${b-unset}]"

p - field splitting - 1
unset a b c
IFS=' -' read a b c <<\END
 AA B CC 
END
p $? "[${a-unset}]" "[${b-unset}]" "[${c-unset}]"

p - field splitting - 2-1
unset a b c d e
IFS=' -' read a b c d e <<\END
-BB-C-DD-
END
p $? "[${a-unset}]" "[${b-unset}]" "[${c-unset}]" "[${d-unset}]" "[${e-unset}]"

p - field splitting - 2-2
unset a b c d e
IFS=' -' read a b c d e <<\END
- BB- C- DD- 
END
p $? "[${a-unset}]" "[${b-unset}]" "[${c-unset}]" "[${d-unset}]" "[${e-unset}]"

p - field splitting - 2-3
unset a b c d e
IFS=' -' read a b c d e <<\END
 -BB -C -DD -
END
p $? "[${a-unset}]" "[${b-unset}]" "[${c-unset}]" "[${d-unset}]" "[${e-unset}]"

p - field splitting - 2-4
unset a b c d e
IFS=' -' read a b c d e <<\END
 - BB - C - DD - 
END
p $? "[${a-unset}]" "[${b-unset}]" "[${c-unset}]" "[${d-unset}]" "[${e-unset}]"

p - field splitting - 3-1
unset a b c d e
IFS=' -' read a b c d e <<\END
--CC--
END
p $? "[${a-unset}]" "[${b-unset}]" "[${c-unset}]" "[${d-unset}]" "[${e-unset}]"

p - field splitting - 3-2
unset a b c d e
IFS=' -' read a b c d e <<\END
  --CC  --
END
p $? "[${a-unset}]" "[${b-unset}]" "[${c-unset}]" "[${d-unset}]" "[${e-unset}]"

p - field splitting - 3-3
unset a b c d e
IFS=' -' read a b c d e <<\END
-  -CC-  -
END
p $? "[${a-unset}]" "[${b-unset}]" "[${c-unset}]" "[${d-unset}]" "[${e-unset}]"

p - field splitting - 3-4
unset a b c d e
IFS=' -' read a b c d e <<\END
--  CC--  
END
p $? "[${a-unset}]" "[${b-unset}]" "[${c-unset}]" "[${d-unset}]" "[${e-unset}]"

p - backslash prevents field splitting - backslash not in IFS
unset a b c d
IFS=' -' read a b c d <<\END
A\ A \ \B\  C\\C\-C\\-D
END
p $? "[${a-unset}]" "[${b-unset}]" "[${c-unset}]" "[${d-unset}]"

p - backslash prevents field splitting - backslash in IFS
unset a b c d
IFS=' -\' read a b c d <<\END
A\ A \ \B\  C\\C\-C\\-D
END
p $? "[${a-unset}]" "[${b-unset}]" "[${c-unset}]" "[${d-unset}]"

p - variables are assigned empty string for missing fields
unset a b c d
read a b c d <<\END
A B
END
p $? "[${a-unset}]" "[${b-unset}]" "[${c-unset}]" "[${d-unset}]"

p - too many fields are joined with trailing whitespaces removed
unset a b c
IFS=' -' read a b c <<\END
A B C-C C\\C\
C   
END
p $? "[${a-unset}]" "[${b-unset}]" "[${c-unset}]"

p - the same, with non-whitespace delimiter
unset a b c
IFS=' -' read a b c <<\END
A B C-C C\\C\
C -  
END
p $? "[${a-unset}]" "[${b-unset}]" "[${c-unset}]"

p - no field splitting with empty IFS
unset a b c d
IFS= read a b c d <<\END
 A\ B \ \C\  D\\E\-F\\-G 
END
p $? "[${a-unset}]" "[${b-unset}]" "[${c-unset}]" "[${d-unset}]"

p - raw mode - backslash not in IFS
unset a b c d
IFS=' -' read -r a b c d <<\END
A\A\\ B-C\- D\
X
END
p $? "[${a-unset}]" "[${b-unset}]" "[${c-unset}]" "[${d-unset}]"

p - raw mode - backslash in IFS
unset a b c d e f
IFS=' -\' read -r a b c d e f <<\END
A\B\\ D-E\- F\
X
END
p $? "[${a-unset}]" "[${b-unset}]" "[${c-unset}]" "[${d-unset}]" \
    "[${e-unset}]" "[${f-unset}]"

p - in subshell
unset a
(echo A | read a)
p $? "[${a-unset}]"

p - failure by readonly variable
! echo B | (readonly a=A; read a 2>/dev/null)
p $?

# printf.y.tst: yash-specific test of the echo and printf builtins
# vim: set ft=sh ts=8 sts=4 sw=4 noet:

echo ===== echo =====

unset ECHO_STYLE

echo 123 456 789
echo 1 22 '3  3' "4
4" 5\
5

testecho() {
    echo ===== ${ECHO_STYLE-unset}
    echo -n new
    echo line
    echo -n new line
    echo '1\a2\b3\c4' 5
    echo '6\f7\n8\r9\t0\v!'
    echo '\0123\012\01x' '\123\12\1x' '\00411'
    echo -e '1\c2'
    echo -e -E '1\c2'
    echo -eE '1\c2'
    echo -ne 123 '-\c-' 456
    echo
}

testecho
ECHO_STYLE=SYSV testecho
ECHO_STYLE=XSI  testecho
ECHO_STYLE=BSD  testecho
ECHO_STYLE=GNU  testecho
ECHO_STYLE=ZSH  testecho
ECHO_STYLE=DASH testecho
ECHO_STYLE=RAW  testecho


echo ===== printf =====

printf ''
printf '1\n'
printf -- 'hello\n' ignored arguments
printf 'multiple\nlines '
printf 'continued\n'

echo =====

printf 'a\ab\bf\fr\rt\tv\v-\n'
printf 'backslash \\\n'
printf 'double-quote \"\n'
printf "single-quote \\'\n"
printf 'octal \11 \1000\n'

echo ===== %%

printf '%%\n'
printf '%%\n' ignored arguments

echo ===== %d

printf '%d\n'
printf '%d\n' 0
printf '%d ' 0 10 200 +345 -1278; echo 1
printf '%+d ' 1 20 +345 -1278; echo 2
printf '%+ d ' 1 20 +345 -1278; echo 3
printf '% d ' 1 20 +345 -1278; echo 4
printf '%+8d ' 1 20 +345 -1278; echo 5
printf '%+-8d ' 1 20 +345 -1278; echo 6
printf '%+08d ' 1 20 +345 -1278; echo 7
printf '%+-08d ' 1 20 +345 -1278; echo 8
printf '%+8.3d ' 1 20 +345 -1278; echo 9
printf '%d ' 00 011 0x1fE 0X1Fe \'a \"@; echo 10

echo ===== %i

printf '%i\n'
printf '%i\n' 0
printf '%i ' 0 10 200 +345 -1278; echo 1
printf '%+i ' 1 20 +345 -1278; echo 2
printf '%+ i ' 1 20 +345 -1278; echo 3
printf '% i ' 1 20 +345 -1278; echo 4
printf '%+8i ' 1 20 +345 -1278; echo 5
printf '%+-8i ' 1 20 +345 -1278; echo 6
printf '%+08i ' 1 20 +345 -1278; echo 7
printf '%+-08i ' 1 20 +345 -1278; echo 8
printf '%+8.3i ' 1 20 +345 -1278; echo 9
printf '%i ' 00 011 0x1fE 0X1Fe \'a \"@; echo 10

echo ===== %u

printf '%u\n'
printf '%u\n' 0
printf '%u ' 0 10 200 +345; echo 1
printf '%+u ' 1 20 +345; echo 2
printf '%+ u ' 1 20 +345; echo 3
printf '% u ' 1 20 +345; echo 4
printf '%8u ' 1 20 +345; echo 5
printf '%-8u ' 1 20 +345; echo 6
printf '%08u ' 1 20 +345; echo 7
printf '%-08u ' 1 20 +345; echo 8
printf '%8.3u ' 1 20 +345; echo 9
printf '%u ' 00 011 0x1fE 0X1Fe \'a \"@; echo 10

echo ===== %o

printf '%o\n'
printf '%o\n' 0
printf '%o ' 0 10 200 +345; echo 1
printf '%+o ' 1 20 +345; echo 2
printf '%+ o ' 1 20 +345; echo 3
printf '% o ' 1 20 +345; echo 4
printf '%8o ' 1 20 +345; echo 5
printf '%-8o ' 1 20 +345; echo 6
printf '%08o ' 1 20 +345; echo 7
printf '%-08o ' 1 20 +345; echo 8
printf '%8.3o ' 1 20 +345; echo 9
printf '%o ' 00 011 0x1fE 0X1Fe \'a \"@; echo 10
printf '%#.3o ' 0 3 010 0123 012345; echo 11

echo ===== %x

printf '%x\n'
printf '%x\n' 0
printf '%x ' 0 10 200 +345; echo 1
printf '%+x ' 1 20 +345; echo 2
printf '%+ x ' 1 20 +345; echo 3
printf '% x ' 1 20 +345; echo 4
printf '%8x ' 1 20 +345; echo 5
printf '%-8x ' 1 20 +345; echo 6
printf '%08x ' 1 20 +345; echo 7
printf '%-08x ' 1 20 +345; echo 8
printf '%8.3x ' 1 20 +345; echo 9
printf '%x ' 00 011 0x1fE 0X1Fe \'a \"@; echo 10
printf '%#x ' 00 011 0x1fE 0X1Fe \'a \"@; echo 11

echo ===== %X

printf '%X\n'
printf '%X\n' 0
printf '%X ' 0 10 200 +345; echo 1
printf '%+X ' 1 20 +345; echo 2
printf '%+ X ' 1 20 +345; echo 3
printf '% X ' 1 20 +345; echo 4
printf '%8X ' 1 20 +345; echo 5
printf '%-8X ' 1 20 +345; echo 6
printf '%08X ' 1 20 +345; echo 7
printf '%-08X ' 1 20 +345; echo 8
printf '%8.3X ' 1 20 +345; echo 9
printf '%X ' 00 011 0x1fE 0X1Fe \'a \"@; echo 10
printf '%#X ' 00 011 0x1fE 0X1Fe \'a \"@; echo 11

echo ===== %f

printf '%f\n'
printf '%f\n' 0
printf '%f ' 1 1.25 -1000.0; echo 1
printf '%+f ' 1 1.25 -1000.0; echo 2
printf '%+ f ' 1 1.25 -1000.0; echo 3
printf '% f ' 1 1.25 -1000.0; echo 4
printf '%+15f ' 1 1.25 -1000.0; echo 5
printf '%+-15f ' 1 1.25 -1000.0; echo 6
printf '%+015f ' 1 1.25 -1000.0; echo 7
printf '%+-015f ' 1 1.25 -1000.0; echo 8
printf '%+15.3f ' 1 1.25 -1000.0; echo 9
printf '%.0f ' 1 1.25 -1000.0; echo 10
printf '%#.0f ' 1 1.25 -1000.0; echo 11

echo ===== %F

printf '%F\n'
printf '%F\n' 0
printf '%F ' 1 1.25 -1000.0; echo 1
printf '%+F ' 1 1.25 -1000.0; echo 2
printf '%+ F ' 1 1.25 -1000.0; echo 3
printf '% F ' 1 1.25 -1000.0; echo 4
printf '%+15F ' 1 1.25 -1000.0; echo 5
printf '%+-15F ' 1 1.25 -1000.0; echo 6
printf '%+015F ' 1 1.25 -1000.0; echo 7
printf '%+-015F ' 1 1.25 -1000.0; echo 8
printf '%+15.3F ' 1 1.25 -1000.0; echo 9
printf '%.0F ' 1 1.25 -1000.0; echo 10
printf '%#.0F ' 1 1.25 -1000.0; echo 11

echo ===== %e

printf '%e\n'
printf '%e\n' 0
printf '%e ' 1 1.25 -1000.0; echo 1
printf '%+e ' 1 1.25 -1000.0; echo 2
printf '%+ e ' 1 1.25 -1000.0; echo 3
printf '% e ' 1 1.25 -1000.0; echo 4
printf '%+15e ' 1 1.25 -1000.0; echo 5
printf '%+-15e ' 1 1.25 -1000.0; echo 6
printf '%+015e ' 1 1.25 -1000.0; echo 7
printf '%+-015e ' 1 1.25 -1000.0; echo 8
printf '%+15.3e ' 1 1.25 -1000.0; echo 9
printf '%.0e ' 1 1.25 -1000.0; echo 10
printf '%#.0e ' 1 1.25 -1000.0; echo 11

echo ===== %E

printf '%E\n'
printf '%E\n' 0
printf '%E ' 1 1.25 -1000.0; echo 1
printf '%+E ' 1 1.25 -1000.0; echo 2
printf '%+ E ' 1 1.25 -1000.0; echo 3
printf '% E ' 1 1.25 -1000.0; echo 4
printf '%+15E ' 1 1.25 -1000.0; echo 5
printf '%+-15E ' 1 1.25 -1000.0; echo 6
printf '%+015E ' 1 1.25 -1000.0; echo 7
printf '%+-015E ' 1 1.25 -1000.0; echo 8
printf '%+15.3E ' 1 1.25 -1000.0; echo 9
printf '%.0E ' 1 1.25 -1000.0; echo 10
printf '%#.0E ' 1 1.25 -1000.0; echo 11

echo ===== %g

printf '%g\n'
printf '%g\n' 0
printf '%g ' 1 1.25 -1000.0 0.000025; echo 1
printf '%+g ' 1 1.25 -1000.0 0.000025; echo 2
printf '%+ g ' 1 1.25 -1000.0 0.000025; echo 3
printf '% g ' 1 1.25 -1000.0 0.000025; echo 4
printf '%+15g ' 1 1.25 -1000.0 0.000025; echo 5
printf '%+-15g ' 1 1.25 -1000.0 0.000025; echo 6
printf '%+015g ' 1 1.25 -1000.0 0.000025; echo 7
printf '%+-015g ' 1 1.25 -1000.0 0.000025; echo 8
printf '%+15.3g ' 1 1.25 -1000.0 0.000025; echo 9
printf '%+#15.3g ' 1 1.25 -1000.0 0.000025; echo 10

echo ===== %G

printf '%G\n'
printf '%G\n' 0
printf '%G ' 1 1.25 -1000.0 0.000025; echo 1
printf '%+G ' 1 1.25 -1000.0 0.000025; echo 2
printf '%+ G ' 1 1.25 -1000.0 0.000025; echo 3
printf '% G ' 1 1.25 -1000.0 0.000025; echo 4
printf '%+15G ' 1 1.25 -1000.0 0.000025; echo 5
printf '%+-15G ' 1 1.25 -1000.0 0.000025; echo 6
printf '%+015G ' 1 1.25 -1000.0 0.000025; echo 7
printf '%+-015G ' 1 1.25 -1000.0 0.000025; echo 8
printf '%+15.3G ' 1 1.25 -1000.0 0.000025; echo 9
printf '%+#15.3G ' 1 1.25 -1000.0 0.000025; echo 10

echo ===== %c

printf '%c\n'
printf '%c\n' ''
printf '%c\n' a
printf '%c\n' long arguments
printf '%3c\n' b
printf '%-3c\n' c

echo ===== %s

printf '%s\n'
printf '%s\n' 'argument  with  space and
newline'
printf '%s\n' 'argument with backslash \\ and percent %%'
printf '%5s ' 123 a long_argument; echo 1
printf '%5.2s ' 123 a long_argument; echo 2
printf '%-5s ' 123 a long_argument; echo 3

echo ===== %b

printf '%b\n'
printf '%b\n' 'argument  with  space and
newline'
printf '%b\n' 'argument with backslash \\ and percent %%'
printf '%5b ' 123 a long_argument; echo 1
printf '%5.2b ' 123 a long_argument; echo 2
printf '%-5b ' 123 a long_argument; echo 3
printf '%-5.2b ' 123 a long_argument; echo 4
printf '%b\n' '1\a2\b3\c4' 5
printf '%b\n' '6\f7\n8\r9\t0\v!'
printf '%b\n' '\0123\012\01x' '\123\12\1x' '\00411'

echo =====

printf '%d\n' not_a_integer >/dev/null 2>&1
echo $?

echo $(printf '%d\n' not_a_integer 2>/dev/null | wc -l)

echo ===== error =====

printf --no-such-option
echo printf no-such-option $?
(printf foo >&- 2>/dev/null)
echo printf output error $?
printf
echo printf operand missing $?
printf '%d\n' foo 2>/dev/null
echo printf invalid operand $?
(echo foo >&- 2>/dev/null)
echo echo output error $?

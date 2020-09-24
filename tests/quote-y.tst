# quote-y.tst: yash-specific test of quoting

(
setup -d

posix="true"

# POSIX does not imply that quote removal should be applied before the expanded
# word is assigned to the unset/empty variable. However, existing shells seem
# to perform quote removal, so yash follows them. Also note that the resultant
# value of the parameter expansion has quote removal already applied, so it is
# subject to field splitting.
test_oE 'quotes in substitution of expansion ${a=b}'
bracket ${a=\ \!\$x\%\&\(\)\*\+\,\-\.\/ \# \"x\" \'x\'}
bracket ${b=\0\1\2\3\4\5\6\7\8\9\:\;\<\=\>\? \\ \\\\}
bracket ${c=\@\A\B\C\D\E\F\G\H\I\J\K\L\M\N\O\P\Q\R\S\T\U\V\W\X\Y\Z\[\]\^\_}
bracket ${d=\a\b\c\d\e\f\g\h\i\j\k\l\m\n\o\p\q\r\s\t\u\v\w\x\y\z\{\|\}\~ \`\`}
bracket ${e=a"b"c} ${f=a"*"c} ${g=a"\"\""c} ${h=a"\\"c} ${i=a"''"c}
bracket ${j=a'b'c} ${k=a'*'c} ${l=a'""'c}   ${m=a'\'c}
bracket $a
bracket $b
bracket $c
bracket $d
bracket $e $f $g $h $i
bracket $j $k $l $m
__IN__
[!$x%&()*+,-./][#]["x"]['x']
[0123456789:;<=>?][\][\\]
[@ABCDEFGHIJKLMNOPQRSTUVWXYZ[]^_]
[abcdefghijklmnopqrstuvwxyz{|}~][``]
[abc][a*c][a""c][a\c][a''c]
[abc][a*c][a""c][a\c]
[!$x%&()*+,-./][#]["x"]['x']
[0123456789:;<=>?][\][\\]
[@ABCDEFGHIJKLMNOPQRSTUVWXYZ[]^_]
[abcdefghijklmnopqrstuvwxyz{|}~][``]
[abc][a*c][a""c][a\c][a''c]
[abc][a*c][a""c][a\c]
__OUT__

# \{ and \} are tested below
test_oE 'quotes in substitution of expansion ${a=b} in double quotes'
bracket "${a=\ \!\$x\%\&\(\)\*\+\,\-\.\/ \# \"x\" \'x\'}"
bracket "${b=\0\1\2\3\4\5\6\7\8\9\:\;\<\=\>\? \\ \\\\}"
bracket "${c=\@\A\B\C\D\E\F\G\H\I\J\K\L\M\N\O\P\Q\R\S\T\U\V\W\X\Y\Z\[\]\^\_}"
bracket "${d=\a\b\c\d\e\f\g\h\i\j\k\l\m\n\o\p\q\r\s\t\u\v\w\x\y\z\|\~ \`\`}"
bracket "${e=a"b"c}" "${f=a"*"c}" "${g=a"\"\""c}" "${h=a"\\"c}" "${i=a"''"c}"
bracket "${j=a'b'c}" "${k=a'*'c}" "${l=a'""'c}"   "${m=a'\'c}"
bracket "$a"
bracket "$b"
bracket "$c"
bracket "$d"
bracket "$e" "$f" "$g" "$h" "$i"
bracket "$j" "$k" "$l" "$m"
__IN__
[\ \!$x\%\&\(\)\*\+\,\-\.\/ \# "x" \'x\']
[\0\1\2\3\4\5\6\7\8\9\:\;\<\=\>\? \ \\]
[\@\A\B\C\D\E\F\G\H\I\J\K\L\M\N\O\P\Q\R\S\T\U\V\W\X\Y\Z\[\]\^\_]
[\a\b\c\d\e\f\g\h\i\j\k\l\m\n\o\p\q\r\s\t\u\v\w\x\y\z\|\~ ``]
[abc][a*c][a""c][a\c][a''c]
[a'b'c][a'*'c][a''c][a'\'c]
[\ \!$x\%\&\(\)\*\+\,\-\.\/ \# "x" \'x\']
[\0\1\2\3\4\5\6\7\8\9\:\;\<\=\>\? \ \\]
[\@\A\B\C\D\E\F\G\H\I\J\K\L\M\N\O\P\Q\R\S\T\U\V\W\X\Y\Z\[\]\^\_]
[\a\b\c\d\e\f\g\h\i\j\k\l\m\n\o\p\q\r\s\t\u\v\w\x\y\z\|\~ ``]
[abc][a*c][a""c][a\c][a''c]
[a'b'c][a'*'c][a''c][a'\'c]
__OUT__

# The same as above, POSIX does not imply quote removal but many existing
# shells perform quote removal.
test_Oe -e 2 'quotes in substitution of expansion ${a?b}'
eval '${u?\ \!\$x\%\&\(\)\*\+\,\-\.\/ \# \"x\" \'\''x\'\''\?\\\`\`"x"'\''y'\''}'
__IN__
eval: u:  !$x%&()*+,-./ # "x" 'x'?\``xy
__ERR__

test_Oe -e 2 'quotes in substitution of expansion ${a?b} in double quotes'
eval '"${u?\ \!\$x\%\&\(\)\*\+\,\-\.\/ \# \"x\" \'\''x\'\''\?\\\\\`\`"x"'\''y'\''}"'
__IN__
eval: u: \ \!$x\%\&\(\)\*\+\,\-\.\/ \# "x" \'x\'\?\\``x'y'
__ERR__

# POSIX XCU 2.2.3 says, "A preceding <backslash> character shall be used to
# escape a literal '{' or '}'." On the other hand, it also says in the same
# section, "The <backslash> shall retain its special meaning as an escape
# character ... only when followed by one of the following characters when
# considered special: $ ` " \ <newline>"
# These specs look quite contradictory as to whether \{ and \} should be
# considered as escapes or not, but existing (other) shells seem to behave
# consistently, so yash follows those shells.
test_oe -e 2 '\{ and \} in substitution of expansions in double quotes'
a=a
bracket "${a+1\{2\}3}" "${u-1\{2\}3}" "${b=1\{2\}3}"
bracket "$b"
eval '"${u?1\{2\}3}"'
__IN__
[1\{2}3][1\{2}3][1\{2}3]
[1\{2}3]
__OUT__
eval: u: 1\{2}3
__ERR__

)

test_oE 'backslash preceding EOF is ignored'
"$TESTEE" -c 'printf "[%s]\n" 123\'
__IN__
[123]
__OUT__

test_oE 'line continuation in function definition'
\
f\
u\
n\
c\
t\
i\
o\
\
n\
	\
f"u\
n"c \
(\
 )\
\
{ echo foo; }
func
__IN__
foo
__OUT__

test_oE 'line continuation in parameter expansion'
f=foo
# echo ${#?} ${${f}} ${f[1,2]:+x}
echo \
$\
{\
\
#\
\
?\
\
} $\
\
{\
\
$\
\
{\
\
f\
\
}\
\
} $\
{\
f\
\
[\
\
1\
\
,\
\
2\
\
]\
\
:\
\
+\
\
x\
\
}
__IN__
1 foo x
__OUT__

test_Oe -e 2 'unclosed single quotation'
echo 'foo
-
__IN__
syntax error: the single quotation is not closed
__ERR__
#'

test_Oe -e 2 'unclosed double quotation (direct)'
echo "foo
__IN__
syntax error: the double quotation is not closed
__ERR__
#"

test_Oe -e 2 'unclosed double quotation (in parameter expansion)'
echo ${foo-"bar}
__IN__
syntax error: the double quotation is not closed
syntax error: `}' is missing
__ERR__
#'
#`
#"

# vim: set ft=sh ts=8 sts=4 sw=4 noet:

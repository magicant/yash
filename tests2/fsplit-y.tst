# fsplit-p.tst: yash-specific test of field splitting

setup -d

test_o 'default IFS'
printf "[%s]\n" "$IFS"
IFS=X "$TESTEE" -c 'printf "[%s]\n" "$IFS"'
__IN__
[ 	
]
[ 	
]
__OUT__

test_o 'modified IFS affects following expansions in single simple command'
unset IFS
v='1  2  3'
bracket $v "${IFS=X}" $v
__IN__
[1][2][3][X][1  2  3]
__OUT__

# vim: set ft=sh ts=8 sts=4 sw=4 noet:

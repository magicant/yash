# quote-y.tst: yash-specific test of quoting

test_oE 'backslash preceding EOF is ignored'
"$TESTEE" -c 'printf "[%s]\n" 123\'
__IN__
[123]
__OUT__

# vim: set ft=sh ts=8 sts=4 sw=4 noet:

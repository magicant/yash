# simple-y.tst: yash-specific test of simple commands

setup -d

test_oE 'words are expanded in order of appearance'
a=1-2-3 IFS=
bracket $a ${IFS:= -} $a
__IN__
[1-2-3][][][1][2][3]
__OUT__

# vim: set ft=sh ts=8 sts=4 sw=4 noet:

# read-p.tst: test of the read built-in for any POSIX-compliant shell

posix="true"
setup -d

test_oE 'single operand - without IFS'
read a <<\END
A
END
echoraw $? "[${a-unset}]"
__IN__
0 [A]
__OUT__

# vim: set ft=sh ts=8 sts=4 sw=4 noet:

# input.p.tst: test of input processing for any POSIX-compliant shell
# vim: set ft=sh ts=8 sts=4 sw=4 noet:

$INVOKE $TESTEE -iv +m input.p.t

$INVOKE $TESTEE <<END
cat
echo
END

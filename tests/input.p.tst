# input.p.tst: test of input processing for any POSIX-compliant shell
# vim: set ft=sh ts=8 sts=4 sw=4 noet:

# The root user's default $PS1 value is not defined in POSIX,
# so we don't test it.
$INVOKE $TESTEE -iv +m input.p.t 3>&2 2>/dev/null

$INVOKE $TESTEE <<END
cat
echo
END

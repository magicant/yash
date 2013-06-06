# input.y.tst: yash-specific test of input processing
# vim: set ft=sh ts=8 sts=4 sw=4 noet:

# The root user has a special default $PS1 value, so we don't test it here.
# See prompt.y.tst.

echo ===== input.p.t =====
$INVOKE $TESTEE -iv +m --norcfile input.p.t 3>&2 2>/dev/null
echo ===== input.t =====
$INVOKE $TESTEE -iv +m --norcfile input.y.t 3>&2 2>/dev/null
# test of '\!' is in history.t
# TODO test of '\j' in PS1/PS2

echo ===== input.p.t =====
$INVOKE $TESTEE -iv +m --norcfile input.p.t
echo ===== input.t =====
$INVOKE $TESTEE -iv +m --norcfile input.y.t
# test of '\!' is in history.t
# TODO test of '\j' in PS1/PS2

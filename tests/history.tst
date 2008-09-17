tmphist="${TESTTMP}/history"

command -V fc

unset FCEDIT
HISTFILE="$tmphist" HISTSIZE=20 $INVOKE $TESTEE -i --norc history.t

echo ==========
cat "$tmphist"
rm -f "$tmphist"

echo ===== 1 =====
HISTFILE="$tmphist" HISTSIZE=20 $INVOKE $TESTEE -i --norc history.2.t

rm -f "$tmphist"

export TMPHIST="${TESTTMP}/history"
export RC="${TESTTMP}/history.rc"

cat >"$RC" <<\END
PS1='' PS2='' HISTFILE="$TMPHIST" HISTSIZE=30
END

command -V fc

unset FCEDIT
$INVOKE $TESTEE -i --rcfile="$RC" <<\EOF
echo 1
echo 2
echo 3
fc -l
echo 5
echo 6
echo 7
echo 8
echo 9
echo 10
echo 11
echo 12
echo 13
echo 14
echo 15
echo 16
echo 17
echo 18
echo 19
fc -l
fc -l -1
fc -l 10 13
fc -l 23 21
fc -l -r 10 13
fc -l -r 23 21
fc -l -n 'echo 1' 20
echo 27
echo 28
echo 29
fc -s
fc -s 27
fc -s -q
fc -s fc
fc -s 1=18
echo 35
echo 36
echo 37
echo 38
echo 39
fc -e true               #40
FCEDIT=true fc           #41
fc -e false 2>&1         #42
fcedit() { echo 'echo ok' >>"$1"; }
fc -e fcedit 36 38       #43-46
fc -e fcedit 38 36       #47-50
fc -e fcedit -rq 38 36   #51-54
fc -l -2
echo =====
$INVOKE $TESTEE -i --rcfile="$RC" <<\EOF2
echo inner shell; fc -l 53
EOF2
echo =====
exit
echo not executed
EOF

echo ==========

$INVOKE $TESTEE -i --rcfile="$RC" <<\EOF
echo 31
echo 32
echo 33
fc -l
EOF

rm -f "$TMPHIST" "$RC"
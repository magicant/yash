# history.y.tst: yash-specific test of history
# vim: set ft=sh ts=8 sts=4 sw=4 noet:

export TMPHIST="${TESTTMP}/history.y.tmp"
export RC="${TESTTMP}/history.y.rc"

cat >"$RC" <<\END
PS1='' PS2='' HISTFILE="$TMPHIST" HISTSIZE=30
unset HISTRMDUP
END

command -V fc
command -V history | grep -v "^history: a regular built-in "

unset FCEDIT
$INVOKE $TESTEE -i +m --rcfile="$RC" <<\EOF
echo 1
echo 2
echo 3
fc -l
echo $?
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
$INVOKE $TESTEE -i +m --rcfile="$RC" <<\EOF2
echo inner shell; fc -l 53
EOF2
echo =====
exit
echo not executed
EOF

echo ===== 1 =====

$INVOKE $TESTEE -i +m --rcfile="$RC" <<\EOF
echo 31
echo 32
echo 33
fc -l
EOF

echo ===== 2 =====

$INVOKE $TESTEE -i +m --rcfile="$RC" <<\EOF
echo 31
history 5
history -d -2 -d 33
history 5
history -c
echo 1
echo 2
echo 3
history -w -
history -w "${TESTTMP}"/history2 -c
echo a
echo b
echo c
cat "${TESTTMP}"/history2
history -r "${TESTTMP}"/history2 -r - <<\END
echo x

END
history -s 1 -s 2 -s 3
history
rm -f "${TESTTMP}/history2"
EOF

echo ===== 3 =====

cat >"$RC" <<\END
PS1='' PS2='' HISTFILE="$TMPHIST" HISTSIZE=30 HISTRMDUP=3 FCEDIT=true
END

>"$TMPHIST"
$INVOKE $TESTEE -i +m --rcfile="$RC" <<\EOF
echo 1
echo 2
echo 3
echo 4
echo 5
echo 6
echo 7
history -d 2 -d 6; fc -l
fc -l  2 6
fc -lr 2 6
fc -l  6 2
fc -lr 6 2
fc    2 6
fc -r 2 6
fc    6 2
fc -r 6 2
fc -l  -- -10000 3
fc -lr -- -10000 3
fc -l  -- 3 -10000
fc -lr -- 3 -10000
EOF

echo ===== 4 =====

>"$TMPHIST"
$INVOKE $TESTEE -i +m --rcfile="$RC" <<\EOF
history -c; fc -l  # should print nothing with no error
EOF

echo ===== histspace =====

>"$TMPHIST"
$INVOKE $TESTEE -i +m --rcfile="$RC" <<\EOF
echo a
 echo b
 set --histspace
echo c
 echo d
  echo e
 set +o histspace
echo f
 echo g
  fc -l
EOF

echo ===== error =====

fc --no-such-option
echo fc no-such-option $?
#fc -l
#echo fc history-empty 1 $?
fc
echo fc history-empty 2 $?
fc -s
echo fc history-empty 3 $?
#fc -l foo
#echo fc history-empty 4 $?
fc foo
echo fc history-empty 5 $?
fc -s foo
echo fc history-empty 6 $?
history -s 'entry' -s 'dummy 1' -s 'dummy 2' -s 'dummy 3' -s 'dummy 4'
fc -l foo
echo fc no-such-entry 1 $?
fc foo
echo fc no-such-entry 2 $?
fc -s foo
echo fc no-such-entry 3 $?
fc -l entry dummy dummy
echo fc too-many-operands 1 $?
fc entry dummy dummy
echo fc too-many-operands 2 $?
fc -s entry dummy
echo fc too-many-operands 3 $?
history --no-such-option
echo history no-such-option $?
history 1 2
echo history too-many-operands $?
(history >&- 2>/dev/null)
echo history output error $?
history -d foo
echo history no-such-entry $?
history -r "$TMPHIST/no/such/file" $? 2>/dev/null
echo history no-such-file 1 $?
history -w "$TMPHIST/no/such/file" $? 2>/dev/null
echo history no-such-file 2 $?

rm -f "$TMPHIST" "$RC"

tmp="${TESTTMP}/sig.p"

echo ===== kill =====

kill -l >/dev/null

sleep 30 &
kill $!
wait $!
kill -l $?

echo ===== trap =====

trap -- "" USR1
kill -s USR1 $$
echo ok

trap 'echo USR2 trapped' USR2
sleep 30 &
kill -s USR1 $!
kill -s USR2 $!
wait $!
kill -l $?

trap "trap - USR1; echo trapped" USR1
kill -s USR1 $$
echo ok

trap 'echo trapped' USR1 USR2
trap >"$tmp"
trap - USR1
trap command2 USR2
. "$tmp"
kill -s USR1 $$
kill -s USR2 $$

# in subshell traps other than ignore are cleared
trap '' USR1
(trap | grep -v USR1)

# signals that were ignored on entry to a non-interactive shell cannot be
# trapped or reset
$INVOKE $TESTEE -c 'trap - USR1 2>/dev/null; kill -USR1 $$; kill -USR2 $$'
kill -l $?  # prints USR2. USR1 is still ignored

rm -f "$tmp"

command -b ulimit -c 0 2>/dev/null

tmp="${TESTTMP}/sig.p"

exec 2>/dev/null

echo ===== kill =====

kill -l >/dev/null

$INVOKE $TESTEE -c 'kill $$' &
wait $!
kill -l $?

# an asynchronous command prevents itself from being killed by SIGINT/SIGQUIT
$INVOKE $TESTEE -c 'cd /tmp; kill -s INT  $$; echo SIGINT ' &
wait $!
$INVOKE $TESTEE -c 'cd /tmp; kill -s QUIT $$; echo SIGQUIT' &
wait $!

echo ===== trap =====

trap -- "" USR1
kill -s USR1 $$
echo ok 1

trap "" USR1
$INVOKE $TESTEE -c "kill -s USR1 $$ \$\$; echo ok 2"
echo ok 3

trap '' USR1
trap 'echo USR2 trapped' USR2
$INVOKE $TESTEE -c 'kill -s USR1 $$; kill -s USR2 $$; echo not printed' &
wait $!
kill -l $?
echo ok 4

trap 'trap - USR1; echo USR1 trapped' USR1
kill -s USR1 $$
$INVOKE $TESTEE -c 'kill -s USR1 $$'
kill -l $?
echo ok 5

trap 'echo trapped' USR1 USR2
trap >"$tmp"
trap - USR1
trap command2 USR2
. "$tmp"
rm -f "$tmp"
kill -s USR1 $$
kill -s USR2 $$
echo ok 6

# in subshell traps other than ignore are cleared
trap '' USR1
trap 'echo trapped' USR2
(trap | grep -v USR1)
echo ok 7

# signals that were ignored on entry to a non-interactive shell cannot be
# trapped or reset
$INVOKE $TESTEE -c 'trap - USR1 2>/dev/null; kill -USR1 $$; kill -USR2 $$'
kill -l $?  # prints USR2. USR1 is still ignored
$INVOKE $TESTEE -c 'trap : USR1 2>/dev/null; kill -USR1 $$; kill -USR2 $$'
kill -l $?
echo ok 8

# if the first operand is integer, all operands are considered as signal
# specification and these signals' handlers are cancelled
trap '' USR1
trap 2 USR1
$INVOKE $TESTEE -c 'kill -s USR1 $$; echo "not printed"'
kill -l $?  # prints USR1

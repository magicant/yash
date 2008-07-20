tmp="${TESTTMP}/sig.p"

set -m

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


rm -f "$tmp"

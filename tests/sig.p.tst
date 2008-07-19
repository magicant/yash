tmp="${TESTTMP}/sig.p"

echo ===== trap =====

trap -- "" USR1
kill -s USR1 $$
echo ok

exec 3>&2 2>/dev/null
(kill -s USR1 -$$)
echo ok # kill -l $?
exec 2>&3

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

# TODO need kill

rm -f "$tmp"

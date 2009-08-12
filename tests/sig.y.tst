export TFIFO="${TESTTMP}/sig.y"

mkfifo "$TFIFO"

echo ===== kill =====

# SIGTSTP is ignored when job control is active
set -m

kill -s TSTP $$
echo TSTP ignored

sleep 30 3>"$TFIFO" 3>&- &
3<"$TFIFO"
kill -s TTOU %1
wait %1
kill -l $?
sleep 30 3>"$TFIFO" 3>&- &
3<"$TFIFO"
kill -s TSTP %2
wait %2
kill -l $?
bg %1 %2
kill %1 %2
fg %1 %2 2>/dev/null
kill -l $?

set +m

echo =====

# SIGINT, SIGTERM and SIGQUIT are ignored if interactive
$INVOKE $TESTEE -si --norcfile 2>/dev/null <<\END
kill -s INT $$
echo INT ignored
kill -s TERM $$
echo TERM ignored
kill -s QUIT $$
echo QUIT ignored

sleep 30 >"$TFIFO" &
<"$TFIFO"
kill -s TERM %1
wait %1 >/dev/null
kill -l $?
END

# check various syntaxes of kill
kill -s CHLD $$ $$
echo 1
kill -n CHLD $$ $$
echo 2
kill -s 0 $$ $$
echo 3
kill -n 0 $$ $$
echo 4
kill -sCHLD $$ $$
echo 5
kill -nCHLD $$ $$
echo 6
kill -s0 $$ $$
echo 7
kill -n0 $$ $$
echo 8
kill -CHLD $$ $$
echo 9
kill -0 $$ $$
echo 10
kill -s CHLD -- $$ $$
echo 11
kill -n CHLD -- $$ $$
echo 12
kill -s 0 -- $$ $$
echo 13
kill -n 0 -- $$ $$
echo 14
kill -sCHLD -- $$ $$
echo 15
kill -nCHLD -- $$ $$
echo 16
kill -s0 -- $$ $$
echo 17
kill -n0 -- $$ $$
echo 18
kill -CHLD -- $$ $$
echo 19
kill -0 -- $$ $$
echo 20
kill -l >/dev/null
echo 21
kill -l -v >/dev/null
echo 22
kill -v -l >/dev/null
echo 23
kill -lv >/dev/null
echo 24
kill -vl >/dev/null
echo 25
kill -v >/dev/null
echo 26
kill -l -- 3 9 15
echo 27
kill -lv -- 3 9 15 >/dev/null
echo 28

echo ===== trap =====

set -m
trap 'echo trapped' USR1
trap '' USR2
kill -s USR2 0 && kill -s USR1 0&
wait %1
kill -l $?
set +m

rm -f "$TFIFO"

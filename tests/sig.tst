echo ===== kill =====

# SIGQUIT is always ignored
kill -s QUIT $$
echo QUIT ignored

# SIGTTOU and SIGTSTP are ignored when job control is active
set -m
kill -s TTOU $$
echo TTOU ignored
kill -s TSTP $$
echo TSTP ignored

sleep 30 &
kill -s TTOU %1
wait %1
kill -l $?
sleep 30 &
kill -s TSTP %2
wait %2
kill -l $?
bg %1 %2
kill %1 %2
wait %1 %2
kill -l $?

echo

# SIGINT and SIGTERM are ignored if interactive
$INVOKE $TESTEE -si 2>/dev/null <<\END
kill -s INT $$
echo INT ignored
kill -s TERM $$
echo TERM ignored

sleep 30 &
kill -s TERM %1
wait %1 >/dev/null
kill -l $?
END

# check various syntaxes of kill
kill -s QUIT $$ $$
echo 1
kill -n QUIT $$ $$
echo 2
kill -s 3 $$ $$
echo 3
kill -n 3 $$ $$
echo 4
kill -sQUIT $$ $$
echo 5
kill -nQUIT $$ $$
echo 6
kill -s3 $$ $$
echo 7
kill -n3 $$ $$
echo 8
kill -QUIT $$ $$
echo 9
kill -3 $$ $$
echo 10
kill -s QUIT -- $$ $$
echo 11
kill -n QUIT -- $$ $$
echo 12
kill -s 3 -- $$ $$
echo 13
kill -n 3 -- $$ $$
echo 14
kill -sQUIT -- $$ $$
echo 15
kill -nQUIT -- $$ $$
echo 16
kill -s3 -- $$ $$
echo 17
kill -n3 -- $$ $$
echo 18
kill -QUIT -- $$ $$
echo 19
kill -3 -- $$ $$
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

# in subshell traps other than ignore are cleared
set -m
trap 'echo trapped' USR1
trap '' USR2
kill -s USR2 0 && kill -s USR1 0&
wait %1
kill -l $?

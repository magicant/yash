# sig.y.tst: yash-specific test of signal handling
# vim: set ft=sh ts=8 sts=4 sw=4 noet:

command -b ulimit -c 0 2>/dev/null

echo ===== kill =====

# check various syntaxes of kill
kill -s CHLD $$ $$
echo kill 1 $?
kill -n CHLD $$ $$
echo kill 2 $?
kill -s 0 $$ $$
echo kill 3 $?
kill -n 0 $$ $$
echo kill 4 $?
kill -sCHLD $$ $$
echo kill 5 $?
kill -nCHLD $$ $$
echo kill 6 $?
kill -s0 $$ $$
echo kill 7 $?
kill -n0 $$ $$
echo kill 8 $?
kill -CHLD $$ $$
echo kill 9 $?
kill -0 $$ $$
echo kill 10 $?
kill -s CHLD -- $$ $$
echo kill 11 $?
kill -n CHLD -- $$ $$
echo kill 12 $?
kill -s 0 -- $$ $$
echo kill 13 $?
kill -n 0 -- $$ $$
echo kill 14 $?
kill -sCHLD -- $$ $$
echo kill 15 $?
kill -nCHLD -- $$ $$
echo kill 16 $?
kill -s0 -- $$ $$
echo kill 17 $?
kill -n0 -- $$ $$
echo kill 18 $?
kill -CHLD -- $$ $$
echo kill 19 $?
kill -0 -- $$ $$
echo kill 20 $?
kill -l >/dev/null
echo kill 21 $?
kill -l -v >/dev/null
echo kill 22 $?
kill -v -l >/dev/null
echo kill 23 $?
kill -lv >/dev/null
echo kill 24 $?
kill -vl >/dev/null
echo kill 25 $?
kill -v >/dev/null
echo kill 26 $?
kill -l -- 3 9 15
echo kill 27 $?
kill -lv -- 3 9 15 >/dev/null
echo kill 28 $?
kill -s chld $$ $$
echo kill 29 $?
kill -schld $$ $$
echo kill 30 $?
kill -s SIGCHLD $$ $$
echo kill 31 $?
kill -sSIGCHLD $$ $$
echo kill 32 $?
kill -s sigchld $$ $$
echo kill 33 $?
kill -ssigchld $$ $$
echo kill 34 $?
kill -SIGCHLD $$ $$
echo kill 35 $?
kill -SiGcHlD $$ $$
echo kill 36 $?

echo ===== trap =====

trap 'echo trapped' USR1
echo trap 1 $?
trap '' USR2
echo trap 2 $?
while kill -s 0 $$; do sleep 1; done 2>/dev/null&
kill -s USR2 $!
kill -s USR1 $!
wait $!
kill -l $?

trap -p USR1 USR2 INT
echo trap 3 $?
trap -  USR1 USR2 INT
echo trap 4 $?

echo ===== signals =====

export SIG

$INVOKE $TESTEE -i +m --norcfile 2>/dev/null <<\END
# SIGINT, SIGTERM and SIGQUIT are ignored if interactive
kill -s INT $$
echo INT ignored
kill -s TERM $$
echo TERM ignored
kill -s QUIT $$
echo QUIT ignored

# SIGINT and SIGQUIT are still ignored in subshells if job control is disabled
# SIGTERM is not ignored in subshells
$INVOKE $TESTEE -c 'kill -s INT  $$' & \
wait $! >/dev/null
echo INT $?
$INVOKE $TESTEE -c 'kill -s TERM $$' & \
wait $! >/dev/null
kill -l $?
$INVOKE $TESTEE -c 'kill -s QUIT $$' & \
wait $! >/dev/null
echo QUIT $?
END

# `cd's before `kill's are there because core dumps, if any, should be created
# in a temporary directory

echo ===== signals +i +m F =====

for SIG in ABRT ALRM BUS FPE HUP ILL INT KILL PIPE QUIT SEGV TERM USR1 USR2
do
    $INVOKE $TESTEE -c +i +m 'cd "$TESTTMP"; kill -s $SIG $$'
    kill -l $?
done
for SIG in CHLD URG
do
    $INVOKE $TESTEE -c +i +m 'cd "$TESTTMP"; kill -s $SIG $$'
    echo $SIG $?
done

echo ===== signals -i +m F =====

for SIG in ABRT ALRM BUS FPE HUP ILL KILL PIPE SEGV USR1 USR2
do
    $INVOKE $TESTEE -ci +m --norcfile 'cd "$TESTTMP"; kill -s $SIG $$'
    kill -l $?
done
for SIG in CHLD URG
do
    $INVOKE $TESTEE -ci +m --norcfile 'cd "$TESTTMP"; kill -s $SIG $$'
    echo $SIG $?
done

echo ===== signals +i +m T =====

for SIG in ABRT ALRM BUS FPE HUP ILL INT KILL PIPE QUIT SEGV TERM USR1 USR2
do
    $INVOKE $TESTEE -c 'cd "$TESTTMP"; kill -s $SIG $$'
    kill -l $?
done
for SIG in CHLD URG
do
    $INVOKE $TESTEE -c 'cd "$TESTTMP"; kill -s $SIG $$'
    echo $SIG $?
done

echo ===== signals -i +m T =====

$INVOKE $TESTEE -i +m --norcfile 2>/dev/null <<\END
export SIG
for SIG in ABRT ALRM BUS FPE HUP ILL KILL PIPE QUIT SEGV TERM USR1 USR2
do
    $INVOKE $TESTEE -c 'cd "$TESTTMP"; kill -s $SIG $$'
    kill -l $?
done
for SIG in CHLD URG
do
    $INVOKE $TESTEE -c 'cd "$TESTTMP"; kill -s $SIG $$'
    echo $SIG $?
done
for SIG in INT INT INT
do
    $INVOKE $TESTEE -c 'cd "$TESTTMP"; kill -s $SIG $$'
    kill -l $?
done
kill -l $?
END

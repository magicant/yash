# sig.y.tst: yash-specific test of signal handling
# vim: set ft=sh ts=8 sts=4 sw=4 noet:

command -b ulimit -c 0 2>/dev/null

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

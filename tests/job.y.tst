# job.y.tst: yash-specific test of job control
# vim: set ft=sh ts=8 sts=4 sw=4 noet:

# all tests that use /dev/tty must be in this test


echo ===== fg bg suspend =====

set -m +o curstop

sleep   `echo  0`&
fg
$INVOKE $TESTEE -c 'suspend; echo 1'
$INVOKE $TESTEE -c 'suspend; echo 2'
$INVOKE $TESTEE -c 'suspend; echo 3'
jobs
fg %2 3 '%? echo 1'

$INVOKE $TESTEE -c 'suspend; echo 4'
jobs %%
bg
wait %%
$INVOKE $TESTEE -c 'suspend'
$INVOKE $TESTEE -c 'suspend'
$INVOKE $TESTEE -c 'suspend; echo 7'
bg %1 %2 
wait
bg '${'
fg '? echo 7' >/dev/null

$INVOKE $TESTEE -imc --norc 'echo 1; suspend; echo 2'
kill -l $?
bg >/dev/null
wait %1
kill -l $?
fg >/dev/null

set +m


echo ===== test =====

# test the -t operator of the test builtin
exec 3<>/dev/tty 4>&-
[ -t 3 ]; echo test 1 $?
test -t 3; echo test 2 $?
[ -t 4 ]; echo test 3 $?
test -t 4; echo test 4 $?
exec 3>&-


echo ===== error =====
echo ===== error ===== >&2

$INVOKE $TESTEE -m <<\END
fg --no-such-option
echo fg no-such-option $?
exit 100 &
fg >&- 2>/dev/null
END
echo fg output error $?
$INVOKE $TESTEE -m <<\END
fg %100
echo fg no-such-job 1 $?
fg %no_such_job
echo fg no-such-job 2 $?
set --posix
exit 101 & exit 102 &
fg %1 %2
echo fg too many args $?
END
fg
echo fg +m $?

$INVOKE $TESTEE -m <<\END
bg --no-such-option
echo bg no-such-option $?
while kill -0 $$; do sleep 1; done 2>/dev/null&
bg >&- 2>/dev/null
echo bg output error $?
kill %1
END
set -m
bg %100
echo bg no-such-job 1 $?
bg %no_such_job
echo bg no-such-job 2 $?
set +m
bg
echo bg +m $?


echo ===== signals =====

export SIG

$INVOKE $TESTEE -im --norcfile 2>/dev/null <<\END
# SIGINT, SIGTERM and SIGQUIT are ignored if interactive
kill -s INT $$
echo INT ignored
kill -s TERM $$
echo TERM ignored
kill -s QUIT $$
echo QUIT ignored

# but not in subshells
$INVOKE $TESTEE -c 'kill -s INT  $$' & \
wait $! >/dev/null
kill -l $?
$INVOKE $TESTEE -c 'kill -s TERM $$' & \
wait $! >/dev/null
kill -l $?
$INVOKE $TESTEE -c 'kill -s QUIT $$' & \
wait $! >/dev/null
kill -l $?
END

set -m

# SIGTSTP is ignored when job control is active
kill -s TSTP $$
echo TSTP ignored

# but not in subshells
kill -s TTOU 0 && echo 1&
wait %1
kill -l $?
kill -s TSTP 0 && echo 2&
wait %2
kill -l $?
fg %1 %2 >/dev/null

set +m

# `cd's before `kill's are there because core dumps, if any, should be created
# in a temporary directory

echo ===== signals +i -m F =====

for SIG in ABRT ALRM BUS FPE HUP ILL INT KILL PIPE QUIT SEGV TERM USR1 USR2
do
    $INVOKE $TESTEE -c +i -m 'cd "$TESTTMP"; kill -s $SIG $$'
    kill -l $?
done
for SIG in CHLD URG
do
    $INVOKE $TESTEE -c +i -m 'cd "$TESTTMP"; kill -s $SIG $$'
    echo $SIG $?
done

echo ===== signals -i -m F =====

for SIG in ABRT ALRM BUS FPE HUP ILL KILL PIPE SEGV USR1 USR2
do
    $INVOKE $TESTEE -ci -m --norcfile 'cd "$TESTTMP"; kill -s $SIG $$'
    kill -l $?
done
for SIG in CHLD URG
do
    $INVOKE $TESTEE -ci -m --norcfile 'cd "$TESTTMP"; kill -s $SIG $$'
    echo $SIG $?
done

echo ===== signals +i -m T =====

set -m
for SIG in ABRT ALRM BUS FPE HUP ILL INT KILL PIPE QUIT SEGV TERM USR1 USR2
do
    (cd "$TESTTMP"; kill -s $SIG 0)
    kill -l $?
done
for SIG in CHLD URG
do
    (cd "$TESTTMP"; kill -s $SIG 0)
    echo $SIG $?
done
for SIG in TSTP TTIN TTOU STOP
do
    (cd "$TESTTMP"; kill -s $SIG 0; echo $SIG!)
    kill -l $?
    fg %1 >/dev/null
    echo $SIG $?
done
set +m

echo ===== signals -i -m T =====

$INVOKE $TESTEE -i -m --norcfile 2>/dev/null <<\END
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
for SIG in TSTP TTIN TTOU STOP
do
    $INVOKE $TESTEE -c 'cd "$TESTTMP"; kill -s $SIG $$; echo $SIG!'
    kill -l $?
    fg %1 >/dev/null
    echo $SIG $?
done
END

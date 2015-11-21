# sig.y.tst: yash-specific test of signal handling
# vim: set ft=sh ts=8 sts=4 sw=4 noet:

command -b ulimit -c 0 2>/dev/null

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

{
    $INVOKE $TESTEE -c  'trap "echo EXIT  1" EXIT;  ./_no_such_command_ '
    $INVOKE $TESTEE -c  'trap "echo EXIT  2" EXIT; (./_no_such_command_)'
    $INVOKE $TESTEE -ce 'trap "echo EXIT  3" EXIT;  ./_no_such_command_ '
    $INVOKE $TESTEE -ce 'trap "echo EXIT  4" EXIT; (./_no_such_command_)'
    $INVOKE $TESTEE -c  'trap "echo EXIT  5" EXIT;  ./_no_such_command_ ; :'
    $INVOKE $TESTEE -c  'trap "echo EXIT  6" EXIT; (./_no_such_command_); :'
    $INVOKE $TESTEE -ce 'trap "echo EXIT  7" EXIT;  ./_no_such_command_ ; :'
    $INVOKE $TESTEE -ce 'trap "echo EXIT  8" EXIT; (./_no_such_command_); :'
    $INVOKE $TESTEE -c  'trap "echo EXIT  9" EXIT;  ./_no_such_command_ ; (:)'
    $INVOKE $TESTEE -c  'trap "echo EXIT 10" EXIT; (./_no_such_command_); (:)'
    $INVOKE $TESTEE -ce 'trap "echo EXIT 11" EXIT;  ./_no_such_command_ ; (:)'
    $INVOKE $TESTEE -ce 'trap "echo EXIT 12" EXIT; (./_no_such_command_); (:)'
} 2>/dev/null
echo trap 5

# In subshell traps other than ignore are cleared.
# Output of the trap built-in reflects it after first trap modification.
(
    trap '' USR1
    trap 'echo USR2' USR2
    (trap 'echo INT' INT && trap) | sort

    ($INVOKE $TESTEE -c 'kill -USR1 $PPID'; echo USR1 sent 1)
    (trap 'echo INT' INT
	$INVOKE $TESTEE -c 'kill -USR1 $PPID'; echo USR1 sent 2)
    ($INVOKE $TESTEE -c 'kill -USR2 $PPID' && echo not reached 1)
    kill -l $?
    (trap 'echo INT' INT
	$INVOKE $TESTEE -c 'kill -USR2 $PPID'; echo not reached 2)
    kill -l $?
    (trap 'echo INT' INT; $INVOKE $TESTEE -c 'kill -INT $PPID'; :)
)

# "return" in traps are ignored
$INVOKE $TESTEE <<\END
trap 'return; echo not reached' USR1
kill -USR1 $$
echo return ignored
END

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

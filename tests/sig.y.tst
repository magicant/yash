# core files, if any, should be created in a temporary directory
mkdir -p "$TESTTMP/sig.y.tmp"
command -b ulimit -c 0 2>/dev/null

echo ===== kill =====

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

trap -p USR1 USR2 INT
trap -  USR1 USR2 INT

echo ===== signals =====

# SIGTSTP and SIGTTOU are ignored when job control is active
set -m

kill -s TSTP $$
echo TSTP ignored
kill -s TTOU $$
echo TTOU ignored

$INVOKE $TESTEE -c 'kill -s TTOU $$' &
wait %1
kill -l $?
$INVOKE $TESTEE -c 'kill -s TSTP $$' &
wait %2
kill -l $?
fg %1 %2 >/dev/null

set +m

echo =====

# SIGINT, SIGTERM and SIGQUIT are ignored if interactive
$INVOKE $TESTEE -sim --norcfile 2>/dev/null <<\END
kill -s INT $$
echo INT ignored
kill -s TERM $$
echo TERM ignored
kill -s QUIT $$
echo QUIT ignored

$INVOKE $TESTEE -c 'cd "$TESTTMP/sig.y.tmp"; kill -s INT  $$' &
wait %1 >/dev/null
kill -l $?
$INVOKE $TESTEE -c 'cd "$TESTTMP/sig.y.tmp"; kill -s TERM $$' &
wait %1 >/dev/null
kill -l $?
$INVOKE $TESTEE -c 'cd "$TESTTMP/sig.y.tmp"; kill -s QUIT $$' &
wait %1 >/dev/null
kill -l $?
END

echo ===== signals +i +m F =====

export SIG
for SIG in ABRT ALRM BUS FPE HUP ILL INT KILL PIPE QUIT SEGV TERM USR1 USR2
do
	$INVOKE $TESTEE -c +i +m 'cd "$TESTTMP/sig.y.tmp"; kill -s $SIG $$'
	kill -l $?
done
for SIG in CHLD URG
do
	$INVOKE $TESTEE -c +i +m 'cd "$TESTTMP/sig.y.tmp"; kill -s $SIG $$'
	echo $SIG $?
done

echo ===== signals +i -m F =====

for SIG in ABRT ALRM BUS FPE HUP ILL INT KILL PIPE QUIT SEGV TERM USR1 USR2
do
	$INVOKE $TESTEE -c +i -m 'cd "$TESTTMP/sig.y.tmp"; kill -s $SIG $$'
	kill -l $?
done
for SIG in CHLD URG
do
	$INVOKE $TESTEE -c +i -m 'cd "$TESTTMP/sig.y.tmp"; kill -s $SIG $$'
	echo $SIG $?
done

echo ===== signals -i +m F =====

for SIG in ABRT ALRM BUS FPE HUP ILL KILL PIPE SEGV USR1 USR2
do
	$INVOKE $TESTEE -ci +m --norcfile 'cd "$TESTTMP/sig.y.tmp"; kill -s $SIG $$'
	kill -l $?
done
for SIG in CHLD URG
do
	$INVOKE $TESTEE -ci +m --norcfile 'cd "$TESTTMP/sig.y.tmp"; kill -s $SIG $$'
	echo $SIG $?
done

echo ===== signals -i -m F =====

for SIG in ABRT ALRM BUS FPE HUP ILL KILL PIPE SEGV USR1 USR2
do
	$INVOKE $TESTEE -ci -m --norcfile 'cd "$TESTTMP/sig.y.tmp"; kill -s $SIG $$'
	kill -l $?
done
for SIG in CHLD URG
do
	$INVOKE $TESTEE -ci -m --norcfile 'cd "$TESTTMP/sig.y.tmp"; kill -s $SIG $$'
	echo $SIG $?
done

echo ===== signals +i +m T =====

for SIG in ABRT ALRM BUS FPE HUP ILL INT KILL PIPE QUIT SEGV TERM USR1 USR2
do
	$INVOKE $TESTEE -c 'cd "$TESTTMP/sig.y.tmp"; kill -s $SIG $$'
	kill -l $?
done
for SIG in CHLD URG
do
	$INVOKE $TESTEE -c 'cd "$TESTTMP/sig.y.tmp"; kill -s $SIG $$'
	echo $SIG $?
done

echo ===== signals +i -m T =====

set -m
for SIG in ABRT ALRM BUS FPE HUP ILL INT KILL PIPE QUIT SEGV TERM USR1 USR2
do
	$INVOKE $TESTEE -c 'cd "$TESTTMP/sig.y.tmp"; kill -s $SIG $$'
	kill -l $?
done
for SIG in CHLD URG
do
	$INVOKE $TESTEE -c 'cd "$TESTTMP/sig.y.tmp"; kill -s $SIG $$'
	echo $SIG $?
done
for SIG in TSTP TTIN TTOU STOP
do
	$INVOKE $TESTEE -c 'cd "$TESTTMP/sig.y.tmp"; kill -s $SIG $$; echo $SIG!'
	kill -l $?
	fg %1 >/dev/null
	echo $SIG $?
done
set +m

$INVOKE $TESTEE -is +m --norcfile 2>/dev/null <<\END

echo ===== signals -i +m T =====

export SIG
for SIG in ABRT ALRM BUS FPE HUP ILL KILL PIPE QUIT SEGV TERM USR1 USR2
do
	$INVOKE $TESTEE -c 'cd "$TESTTMP/sig.y.tmp"; kill -s $SIG $$'
	kill -l $?
done
for SIG in CHLD URG
do
	$INVOKE $TESTEE -c 'cd "$TESTTMP/sig.y.tmp"; kill -s $SIG $$'
	echo $SIG $?
done
for SIG in INT INT INT
do
	$INVOKE $TESTEE -c 'cd "$TESTTMP/sig.y.tmp"; kill -s $SIG $$'
	kill -l $?
done
kill -l $?

echo ===== signals -i -m T =====

set -m
for SIG in ABRT ALRM BUS FPE HUP ILL KILL PIPE QUIT SEGV TERM USR1 USR2
do
	$INVOKE $TESTEE -c 'cd "$TESTTMP/sig.y.tmp"; kill -s $SIG $$'
	kill -l $?
done
for SIG in CHLD URG
do
	$INVOKE $TESTEE -c 'cd "$TESTTMP/sig.y.tmp"; kill -s $SIG $$'
	echo $SIG $?
done
for SIG in INT INT INT
do
	$INVOKE $TESTEE -c 'cd "$TESTTMP/sig.y.tmp"; kill -s $SIG $$'
	kill -l $?
done
kill -l $?
for SIG in TSTP TTIN TTOU STOP
do
	$INVOKE $TESTEE -c 'cd "$TESTTMP/sig.y.tmp"; kill -s $SIG $$; echo $SIG!'
	kill -l $?
	fg %1 >/dev/null
	echo $SIG $?
done
set +m

END

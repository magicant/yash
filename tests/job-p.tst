# job-p.tst: test of job control for any POSIX-compliant shell
../checkfg || skip="true" # %SEQUENTIAL%

posix="true"

mkfifo sync

test_x -e 17 'job result is not lost when reported automatically (+b)' -im
exec >sync && exit 17 &
pid=$!
cat sync
:
:
:
wait $pid
__IN__

# This test is in async-p.tst.
#test_oE 'stdin of asynchronous list is null without job control' +m

test_oE 'stdin of asynchronous list is not modified with job control' -m
tail -n 1& wait
echo this line should be skipped by tail
echo this line should be printed by tail
__IN__
echo this line should be printed by tail
__OUT__

# These tests are in async-p.tst.
#test_oE -e 0 'asynchronous list ignores SIGINT'
#test_oE -e 0 'asynchronous list ignores SIGQUIT'

test_oE 'asynchronous list retains SIGINT trap with job control' -m
"$TESTEE" -c 'kill -s INT $$; echo not printed' &
wait $!
kill -l $?
trap '' INT
"$TESTEE" -c 'kill -s INT $$; echo ok' &
wait $!
__IN__
INT
ok
__OUT__

test_oE 'asynchronous list retains SIGQUIT trap with job control' -m
"$TESTEE" -c 'kill -s QUIT $$; echo not printed' &
wait $!
kill -l $?
trap '' QUIT
"$TESTEE" -c 'kill -s QUIT $$; echo ok' &
wait $!
__IN__
QUIT
ok
__OUT__

# vim: set ft=sh ts=8 sts=4 sw=4 noet:

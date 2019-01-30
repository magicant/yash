# job-y.tst: yash-specific test of job control
../checkfg || skip="true" # %REQUIRETTY%

mkfifo sync

# According to POSIX, a shell may, but is not required to, forget the job
# when the -b option is on. Yash forgets it.
test_x -e 17 'job result is not lost when reported automatically (-b)' -bim
exec >sync && exit 17 &
pid=$!
cat sync
:
:
:
wait $pid
__IN__

# vim: set ft=sh ts=8 sts=4 sw=4 noet:

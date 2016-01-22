# job-p.tst: test of job control for any POSIX-compliant shell
../checkfg || skip="true" # %SEQUENTIAL%

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

# vim: set ft=sh ts=8 sts=4 sw=4 noet:

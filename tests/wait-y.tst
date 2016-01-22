# wait-y.tst: yash-specific test of the wait built-in

mkfifo sync

test_x -e 127 'job is forgotten after awaited' -im
exec >sync && exit 17 &
pid=$!
cat sync
:
:
:
wait
wait $pid
__IN__

test_Oe -e 2 'invalid option --xxx'
wait --no-such=option
__IN__
wait: `--no-such=option' is not a valid option
__ERR__
#'
#`

# vim: set ft=sh ts=8 sts=4 sw=4 noet:

# kill4-p.tst: test of the kill built-in for any POSIX-compliant shell, part 4
../checkfg || skip="true" # %REQUIRETTY%

posix="true"

# This FIFO is for synchronization between processes. First, it ensures the
# receiver has started before the sender sends a signal. Next, the receiver
# tries to reopen the FIFO so that the receiver does not exit before it
# receives the signal.
mkfifo fifo

# all processes in the same process group
test_oE 'sending signal to process 0' -m
kill -s HUP 0 >fifo | cat fifo fifo
kill -l $?
__IN__
HUP
__OUT__

test_oE 'sending signal with negative process number: -s HUP' -m
(
pgid="$(exec sh -c 'echo $PPID')"
kill -s HUP -- -$pgid >fifo | cat fifo fifo
)
kill -l $?
__IN__
HUP
__OUT__

test_oE 'sending signal with negative process number: -1' -m
(
pgid="$(exec sh -c 'echo $PPID')"
kill -1 -- -$pgid >fifo | cat fifo fifo
)
kill -l $?
__IN__
HUP
__OUT__

(
setup 'halt() while kill -s CONT $$; do sleep 1; done'
mkfifo fifo1 fifo2 fifo3

test_oE 'sending signal to background job' -m
(trap 'echo 1;      exit' USR1; >fifo1; halt) |
(trap 'echo 2; cat; exit' USR1; >fifo2; halt) |
(trap 'echo 3; cat; exit' USR1; >fifo3; halt) &
halt &
<fifo1 <fifo2 <fifo3
kill -s USR1 %'(trap'
wait %'(trap'
kill -s USR2 %halt
wait %halt
kill -l $?
__IN__
3
2
1
USR2
__OUT__

test_oE 'sending to multiple processes' -m
(trap 'echo; exit' TERM; >fifo1; halt X) &
(trap 'echo; exit' TERM; >fifo2; halt Y) &
<fifo1 <fifo2
kill '%?X' $!
wait '%?X' $!
__IN__


__OUT__

)

# vim: set ft=sh ts=8 sts=4 sw=4 noet:

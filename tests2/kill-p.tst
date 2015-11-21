# kill-p.tst: test of the kill built-in for any POSIX-compliant shell
../checkfg || skip="true" # %SEQUENTIAL%

posix="true"

test_E -e 0 'printing all signal names'
kill -l
__IN__

# $1 = LINENO, $2 = signal number, $3 = signal name w/o SIG
test_printing_signal_name_from_number() {
    testcase "$1" -e 0 "printing signal name $3 from number" \
	3<<__IN__ 4<<__OUT__ 5</dev/null
kill -l $2
__IN__
$3
__OUT__
}

test_printing_signal_name_from_number "$LINENO" 1 HUP
test_printing_signal_name_from_number "$LINENO" 2 INT
test_printing_signal_name_from_number "$LINENO" 3 QUIT
test_printing_signal_name_from_number "$LINENO" 6 ABRT
test_printing_signal_name_from_number "$LINENO" 9 KILL
test_printing_signal_name_from_number "$LINENO" 14 ALRM
test_printing_signal_name_from_number "$LINENO" 15 TERM

# $1 = LINENO, $2 = signal name w/o SIG
test_printing_signal_name_from_exit_status() (
    if sh -c "kill -s $2 \$\$"; then
	skip="true"
    fi
    testcase "$1" -e 0 "printing signal name $2 from exit status" \
	3<<__IN__ 4<<__OUT__ 5</dev/null
sh -c 'kill -s $2 \$\$'
kill -l \$?
__IN__
$2
__OUT__
)

test_printing_signal_name_from_exit_status "$LINENO" HUP
test_printing_signal_name_from_exit_status "$LINENO" INT
test_printing_signal_name_from_exit_status "$LINENO" QUIT
test_printing_signal_name_from_exit_status "$LINENO" ABRT
test_printing_signal_name_from_exit_status "$LINENO" KILL
test_printing_signal_name_from_exit_status "$LINENO" ALRM
test_printing_signal_name_from_exit_status "$LINENO" TERM

test_OE -e TERM 'sending default signal TERM'
kill $$
__IN__

test_OE -e 0 'sending null signal: -s 0'
kill -s 0 $$
__IN__

# $1 = LINENO, $2 = signal name w/o SIG, $3 = prefix for $2
test_sending_signal_kill() {
    testcase "$1" -e "$2" "sending signal: $3$2" \
	3<<__IN__ 4</dev/null 5</dev/null
kill $3$2 \$\$
__IN__
}

test_sending_signal_kill "$LINENO" ABRT '-s '
test_sending_signal_kill "$LINENO" ALRM '-s '
test_sending_signal_kill "$LINENO" BUS  '-s '
test_sending_signal_kill "$LINENO" FPE  '-s '
test_sending_signal_kill "$LINENO" HUP  '-s '
test_sending_signal_kill "$LINENO" ILL  '-s '
test_sending_signal_kill "$LINENO" INT  '-s '
test_sending_signal_kill "$LINENO" KILL '-s '
test_sending_signal_kill "$LINENO" PIPE '-s '
test_sending_signal_kill "$LINENO" QUIT '-s '
test_sending_signal_kill "$LINENO" SEGV '-s '
test_sending_signal_kill "$LINENO" TERM '-s '
test_sending_signal_kill "$LINENO" USR1 '-s '
test_sending_signal_kill "$LINENO" USR2 '-s '
test_sending_signal_kill "$LINENO" ABRT '-s '

test_sending_signal_kill "$LINENO" ABRT -
test_sending_signal_kill "$LINENO" ALRM -
test_sending_signal_kill "$LINENO" BUS  -
test_sending_signal_kill "$LINENO" FPE  -
test_sending_signal_kill "$LINENO" HUP  -
test_sending_signal_kill "$LINENO" ILL  -
test_sending_signal_kill "$LINENO" INT  -
test_sending_signal_kill "$LINENO" KILL -
test_sending_signal_kill "$LINENO" PIPE -
test_sending_signal_kill "$LINENO" QUIT -
test_sending_signal_kill "$LINENO" SEGV -
test_sending_signal_kill "$LINENO" TERM -
test_sending_signal_kill "$LINENO" USR1 -
test_sending_signal_kill "$LINENO" USR2 -
test_sending_signal_kill "$LINENO" ABRT -

# $1 = LINENO, $2 = signal name w/o SIG, $3 = prefix for $2
test_sending_signal_ignore() {
    testcase "$1" -e 0 "sending signal: $3$2" \
	3<<__IN__ 4</dev/null 5</dev/null
kill $3$2 \$\$
__IN__
}

test_sending_signal_ignore "$LINENO" CHLD '-s '
test_sending_signal_ignore "$LINENO" CONT '-s '
test_sending_signal_ignore "$LINENO" URG  '-s '

test_sending_signal_ignore "$LINENO" CHLD -
test_sending_signal_ignore "$LINENO" CONT -
test_sending_signal_ignore "$LINENO" URG  -

# $1 = LINENO, $2 = signal name w/o SIG, $3 = prefix for $2
test_sending_signal_stop() {
    testcase "$1" -e 0 "sending signal: $3$2" \
	3<<__IN__ 4</dev/null 5</dev/null
(kill $3$2 \$\$; status=\$?; kill -s CONT \$\$; exit \$status)
__IN__
}

test_sending_signal_stop "$LINENO" STOP '-s '
test_sending_signal_stop "$LINENO" TSTP '-s '
test_sending_signal_stop "$LINENO" TTIN '-s '
test_sending_signal_stop "$LINENO" TTOU '-s '

test_sending_signal_stop "$LINENO" STOP -
test_sending_signal_stop "$LINENO" TSTP -
test_sending_signal_stop "$LINENO" TTIN -
test_sending_signal_stop "$LINENO" TTOU -

# $1 = LINENO, $2 = signal name w/o SIG, $3 = signal number
test_sending_signal_num_kill_self() {
    testcase "$1" -e "$2" "sending signal: -$3" \
	3<<__IN__ 4</dev/null 5</dev/null
kill -$3 \$\$
__IN__
}

test_sending_signal_num_kill_self "$LINENO" HUP  1
test_sending_signal_num_kill_self "$LINENO" INT  2
test_sending_signal_num_kill_self "$LINENO" QUIT 3
test_sending_signal_num_kill_self "$LINENO" ABRT 6
test_sending_signal_num_kill_self "$LINENO" KILL 9
test_sending_signal_num_kill_self "$LINENO" ALRM 14
test_sending_signal_num_kill_self "$LINENO" TERM 15

# all processes in the same process group
test_oE 'sending signal to process 0' -m
kill -s HUP 0 | cat
kill -l $?
__IN__
HUP
__OUT__

test_oE 'sending signal with negative process number: -s HUP' -m
(
pgid="$(exec sh -c 'echo $PPID')"
kill -s HUP -- -$pgid | cat
)
kill -l $?
__IN__
HUP
__OUT__

test_oE 'sending signal with negative process number: -1' -m
(
pgid="$(exec sh -c 'echo $PPID')"
kill -1 -- -$pgid | cat
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

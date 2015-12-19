# ppid-p.tst: test of the $PPID variable for any POSIX-compliant shell

posix="true"

(
SENT=false RECEIVED=false
trap 'RECEIVED=true' USR2
testee -c 'kill -s USR2 $PPID' && SENT=true
export SENT RECEIVED

test_oE 'PPID is parent process ID'
echo sent=$SENT
echo received=$RECEIVED
__IN__
sent=true
received=true
__OUT__

)

test_OE -e 0 'PPID does not change in subshell'
echo $PPID >main.out
(echo $PPID) >subshell.out
diff main.out subshell.out
__IN__

# vim: set ft=sh ts=8 sts=4 sw=4 noet:

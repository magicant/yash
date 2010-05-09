# job.p.tst: test of job control for any POSIX-compliant shell
# vim: set ft=sh ts=8 sts=4 sw=4 noet:

echo echo &
wait
echo wait ok

(echo echo; exit 3)&
wait $!
echo wait $?

true & true & true &
wait
echo wait $?

(exit 1)&
p1=$!
(exit 2)&
p2=$!
(exit 3)&
p3=$!
echo echo
wait $p2 $p3
echo wait $?
wait $p1
echo wait $?
wait $p1
echo wait $?

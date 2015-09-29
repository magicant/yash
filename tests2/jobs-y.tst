# jobs-y.tst: yash-specific test of the jobs & suspend built-ins
../checkfg || skip="true" # %SEQUENTIAL%

test_oE 'suspend: suspending' -m
"$TESTEE" -c 'suspend; echo $?'
echo -
fg >/dev/null
__IN__
-
0
__OUT__

test_oE 'jobs: printing jobs' -m +o curstop
"$TESTEE" -c 'suspend; : 1'
"$TESTEE" -c 'suspend; : 2'
"$TESTEE" -c 'suspend; : 3'
jobs
echo \$?=$?
while fg; do :; done >/dev/null 2>&1
__IN__
[1]   Stopped(SIGSTOP)     "${TESTEE}" -c 'suspend; : 1'
[2] - Stopped(SIGSTOP)     "${TESTEE}" -c 'suspend; : 2'
[3] + Stopped(SIGSTOP)     "${TESTEE}" -c 'suspend; : 3'
$?=0
__OUT__

test_oE 'exit status of suspended job' -m
"$TESTEE" -cim --norcfile 'echo 1; suspend; echo 2'
kill -l $?
bg >/dev/null
wait %
kill -l $?
fg >/dev/null
__IN__
1
STOP
TTOU
2
__OUT__

# vim: set ft=sh ts=8 sts=4 sw=4 noet:

# job.y.tst: yash-specific test of job control
# vim: set ft=sh ts=8 sts=4 sw=4 noet:


echo ===== fg bg suspend =====

set -m +o curstop

sleep   `echo  0`&
fg
$INVOKE $TESTEE -c 'suspend; echo 1'
$INVOKE $TESTEE -c 'suspend; echo 2'
$INVOKE $TESTEE -c 'suspend; echo 3'
jobs
fg %2 3 '%? echo 1'

$INVOKE $TESTEE -c 'suspend; echo 4'
jobs %%
bg
wait %%
$INVOKE $TESTEE -c 'suspend'
$INVOKE $TESTEE -c 'suspend'
$INVOKE $TESTEE -c 'suspend; echo 7'
bg %1 %2 
wait
bg '${'
fg '? echo 7' >/dev/null

$INVOKE $TESTEE -imc --norc 'echo 1; suspend; echo 2'
kill -l $?
bg >/dev/null
wait %1
kill -l $?
fg >/dev/null

set +m


echo ===== error =====
echo ===== error ===== >&2

$INVOKE $TESTEE -m <<\END
fg --no-such-option
echo fg no-such-option $?
exit 100 &
fg >&- 2>/dev/null
END
echo fg output error $?
$INVOKE $TESTEE -m <<\END
fg %100
echo fg no-such-job 1 $?
fg %no_such_job
echo fg no-such-job 2 $?
set --posix
exit 101 & exit 102 &
fg %1 %2
echo fg too many args $?
END
fg
echo fg +m $?

$INVOKE $TESTEE -m <<\END
bg --no-such-option
echo bg no-such-option $?
while kill -0 $$; do sleep 1; done 2>/dev/null&
bg >&- 2>/dev/null
kill %1
END
echo bg output error $?
set -m
bg %100
echo bg no-such-job 1 $?
bg %no_such_job
echo bg no-such-job 2 $?
set +m
bg
echo bg +m $?


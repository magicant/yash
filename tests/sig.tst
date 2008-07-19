echo ===== trap =====

# SIGQUIT is always ignored
kill -s QUIT $$
echo QUIT

# SIGTTOU and SIGTSTP are ignored when job control is active
set -m
kill -s TTOU $$
echo TTOU
kill -s TSTP $$
echo TSTP
set +m

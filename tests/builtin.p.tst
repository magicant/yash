# builtin.p.tst: test of builtins for any POSIX-compliant shell
# vim: set ft=sh ts=8 sts=4 sw=4 noet:

echo ===== special builtins =====

{
    (: <"${TESTTMP}/no.such.file"; echo not reached - redir)
    (readonly ro=ro; ro=xx eval 'echo test'; echo not reached - assign)
    (unset unset; set ${unset?}; echo not reached - expansion)
    (. "${TESTTMP}/no.such.file"; echo not reached - dot not found)
    (break invalid argument; echo not reached - usage error)
} 2>/dev/null


echo ===== exec =====

$INVOKE $TESTEE -c 'exec echo exec'
$INVOKE $TESTEE -c '(exec echo 1); exec echo 2'

exec echo exec echo
echo not reached

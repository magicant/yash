# builtin.y.tst: yash-specific test of builtins
# vim: set ft=sh ts=8 sts=4 sw=4 noet:

echo ===== exec =====

(exec -a sh $TESTEE -c 'echo $0')
(exec --as=sh $TESTEE -c 'echo $0')
(foo=123 bar=456 baz=(7 8 9) exec -c env | grep -v ^_ | sort)
(foo=123 bar=456 baz=(7 8 9) exec --clear env | grep -v ^_ | sort)

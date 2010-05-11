# lineedit.y.tst: yash-specific test of the bindkey/complete builtin
# vim: set ft=sh ts=8 sts=4 sw=4 noet:

# TODO test of bindkey

echo ===== error =====

bindkey --no-such-option
echo bindkey no-such-option $?
bindkey -l >&- 2>/dev/null
echo bindkey output error 1 $?
bindkey -v >&- 2>/dev/null
echo bindkey output error 2 $?
bindkey
echo bindkey operand missing $?
bindkey -v '\\' no-such-command
echo bindkey invalid operand 1 $?
bindkey -v '' abort-line
echo bindkey invalid operand 2 $?

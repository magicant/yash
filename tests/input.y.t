PS1='% '
exec 2>&3
PROMPT_COMMAND='echo prompt_command >&2'
(exit \
1)
echo $?
unset PROMPT_COMMAND
PS1='${PWD##${PWD}}$(echo ?) '
PS1='! !! $ '
PS1='\a \e \n \r \\ $ '
PS1='$ '
PS2='\\ > '
echo \
ok
PS1=

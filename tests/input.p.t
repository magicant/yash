PS1='% '
exec 2>&3
echo \
ok
cat <<END
here-document
ok
END
PS1='${PWD##${PWD}}? '
PS1=

tmp="${TESTTMP}/alias.tmp"

alias -g A=a B=b C=c singlequote=\'
echo C B A -A- -B- -C- \A "B" 'C'
alias --global \C \singlequote C='| cat'
echo pipe alias C

alias -p >"$tmp"
\unalias -a
. "$tmp"
alias --prefix | diff - "$tmp" && echo restored

unalias --all
alias # prints nothing
alias c='cat'
alias -g P=')|c'
echo complex alias | (c|(c P)|c

unalias -a
alias -g a='a a '
echo a a a
alias foobar='FOO=BAR ' e='env'
alias -g G='|grep'
foobar e G '^FOO='

command -V alias unalias

rm -f "$tmp"

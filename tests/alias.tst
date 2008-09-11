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

command -V alias unalias

rm -f "$tmp"

tmp=${TESTTMP}/alias.p.tmp

alias c=cat
echo alias | c

alias echo='echo '
echo c c
\echo c c
ec\
ho echo c c

alias echo='echo hello'
echo world
unalias echo
echo world

alias if=: then=: fi=: 2>/dev/null
if true; then echo reserved words; fi

alias | sort >"$tmp"
\unalias -a
eval alias $(cat "$tmp")
alias | sort | diff - "$tmp" && echo restored

rm -f "$tmp"

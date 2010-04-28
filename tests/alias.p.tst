tmp=${TESTTMP}/alias.p.tmp

alias c=cat alias=alias
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
unalias if then fi 2>/dev/null

alias 8=echo @!,%=echo
8</dev/null echo IO_NUMBER
@!,% @!,%
unalias 8 @!,%

alias | sort >"$tmp"
\unalias -a
eval alias -- $(cat "$tmp")
alias | sort | diff - "$tmp" && echo restored

echo =====

if command -v echo >/dev/null 2>&1; then
	commandv='command -v'
	aliasdef=$($commandv alias)
	unalias alias
	$commandv alias
	eval "$aliasdef"
	[ x"$($commandv alias)" = x"$aliasdef" ] || echo not restored
else
	echo alias
fi
if command -V echo >/dev/null 2>&1; then
	alias pqr=xyz
	if ! command -V pqr | grep pqr | grep xyz >/dev/null; then
		echo "\"command -V (alias)\" doesn't include alias definition" >&2
	fi
fi

rm -f "$tmp"

# alias.p.tst: test of aliases for any POSIX-compliant shell
# vim: set ft=sh ts=8 sts=4 sw=4 noet:

alias alias=alias
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
    case "$(command -V pqr)" in
	*pqr*xyz* | *xyz*pqr* ) ;;
	* )                     printf '%s\n' "$(command -V pqr)" ;;
    esac
fi

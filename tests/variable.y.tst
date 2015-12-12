# variable.y.tst: yash-specific test of variables
# vim: set ft=sh ts=8 sts=4 sw=4 noet:

echo ===== function typeset/unset =====

func () {
    echo ok $?
}
function x=1 {
    echo equal
}
{
    typeset -fp func
    typeset -fp x=1
    typeset -fp
} | sed 's/^ *//'

readonly -f func
{
    func () { echo invalid re-definition; }
} 2>/dev/null
func
unset -f func 2>/dev/null
func

unset -f x=1
typeset -fp x=1 2>/dev/null
func

func2() {
    echo $((echo func2) )
}
eval "$(typeset -fp func2)"
func2


rm -f "${TESTTMP}/variable.y.tm1" "${TESTTMP}/variable.y.tm2"

# alias.y.tst: yash-specific test of aliases
# vim: set ft=sh ts=8 sts=4 sw=4 noet:

tmp="${TESTTMP}/alias.y.tmp"

alias # prints nothing: yash has not predefined aliases
echo $?
alias -g A=a B=b C=c singlequote=\' -- ---=-
echo $? C B A -A- -B- -C- \A "B" 'C' ---
alias --global \C \singlequote C='| cat'
echo pipe alias C

alias -p >"$tmp"
unalias -a
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
alias foobar=' FOO=BAR ' e='env'
alias -g G=' |grep'
foobar e G '^FOO='

unalias -a
alias cc=' ( c ) ' c='cat'
echo ok | cc | cc |
cc |\
cc
alias i=if if=fi
i true; then echo if; fi
alias i='if true; then echo if; fi'
i
alias f=false ff='FOO=BAR f' fff='>/dev/null ff' ffff='! fff'
! f
echo $?
! ff
echo $?
! fff
echo $?
ffff
echo $?
alias -g v=V
FOO=BAR echo v
FOO=BAR echo 3>/dev/null v

unalias -a
alias -g N='>/dev/null'
alias -p \N
echo null N  # prints nothing
N echo null  # prints nothing
(echo null) N  # prints nothing
{ echo null; } N  # prints nothing
if :; then echo null; fi N  # prints nothing
if if :; then :; fi then if :; then echo null; fi fi N  # prints nothing
if if :; then :; fi then if :; then echo null; fi N fi  # prints nothing
if if echo null; then :; fi N then if :; then :; fi fi  # prints nothing

unalias -a
alias test=echo
func() { echo "$(test ok)"; }
func
cat >"$tmp" <<\END
func() { echo "$(test ok)"; }
test foo
END
. -A "$tmp" && . --no-alias "$tmp"
echo $?
func

unalias -a
alias test=echo
alias -g global=alias null='/dev/null' nullout='> null'
te\
st line continuation 1
dummy=dummy te\
st line continuation 2
test line continuation 3 glo\
bal
{ test not printed 4; } nullou\
t
{ test not printed 5; } > nul\
l
array=(foo glo\
bal bar)
test line continuation 6 $array

unalias -a
alias c='cat <<END' lc='echo line \
    continuation...
echo' r='c
multi-line alias \'
c
c
END
lc in alias
r
with here-document
END

unalias -a
$INVOKE $TESTEE --posix <<\END
alias test=:
func() { echo "$(test posix unparsed ok)"; }
alias test=echo
func
END
$INVOKE $TESTEE --posix <<\END
alias dummy=
false
dummy
[ $? -ne 0 ]
echo dummy $?
if
    dummy
then
    echo error
fi
END
$INVOKE $TESTEE --posix <<\END
alias dummy=
echo pipe | dummy
cat
false && dummy
echo not printed
(dummy)
echo error
END
$INVOKE $TESTEE <<\END
alias not=' !'
not false
echo $?
echo error | not cat
echo error
END
$INVOKE $TESTEE <<\END
alias ss=' ;;'
case i in
    i) ss
    *) echo error
esac
true && ss
END
$INVOKE $TESTEE <<\END
alias echo='&&'
true; echo error
END

command -V alias unalias

alias --no-such-option
echo alias no-such-option $?
alias alias
echo alias no-such-alias $?
alias alias=alias
(alias >&- 2>/dev/null)
echo alias output error 1 $?
(alias alias >&- 2>/dev/null)
echo alias output error 2 $?
(alias -p alias >&- 2>/dev/null)
echo alias output error 3 $?
unalias --no-such-option
echo unalias no-such-option $?
unalias alias
unalias alias
echo unalias no-such-alias $?

rm -f "$tmp"

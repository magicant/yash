# alias-p.tst: test of aliases for any POSIX-compliant shell

posix="true"
setup 'set -e'

if testee <<\END
    alias true=false
    true
END
then
    printf '# Skipping tests of aliases (POSIX)\n'
    return
fi

test_OE -e 0 'defining alias'
alias a='echo ABC'
__IN__

(
setup "alias a='echo ABC'"

test_oE -e 0 'using alias'
a
a
a
__IN__
ABC
ABC
ABC
__OUT__

test_OE -e 0 'redefining alias - exit status'
alias a='echo BCD'
__IN__

test_oE 'redefining alias - redefinition'
alias a='echo BCD'
a
__IN__
BCD
__OUT__

test_OE -e 0 'removing specific alias - exit status'
alias true=false
unalias true
__IN__

test_OE -e 0 'removing specific alias - removal'
alias true=false
unalias true
true
__IN__

test_OE -e 0 'removing multiple aliases - exit status'
alias true=a a=b b=false
unalias true a b
__IN__

test_OE -e 0 'removing multiple aliases - removal'
alias true=a a=b b=false
unalias true a b
true
__IN__

test_OE -e 0 'removing all aliases - exit status'
alias a=a b=b c=c
unalias -a
__IN__

test_OE 'removing all aliases - removal'
alias a=a b=b c=c
unalias -a
alias
__IN__

test_OE -e 0 'printing specific alias'
alias a | grep -q '^a='
__IN__

test_oE -e 0 'reusing printed alias (simple)'
save="$(alias a)"
unalias a
eval alias "$save"
a
__IN__
ABC
__OUT__

test_oE -e 0 'reusing printed alias (complex quotation)'
alias a='printf %s\\n \"['\\\'{'\\'}\\\'']\"'
save="$(alias a)"
unalias a
eval alias "$save"
a
__IN__
"['{\}']"
__OUT__

test_OE -e 0 'printing all aliases'
alias b=b c=c e='echo OK'
alias >save_alias_1
unalias -a
IFS='
' # trick to replace each newline in save_alias_1 with a space
eval alias -- $(cat save_alias_1)
alias >save_alias_2
diff save_alias_1 save_alias_2
__IN__

test_OE -e 0 'subshell inherits aliases'
(alias a) | grep -q '^a='
__IN__

test_oE 'subshell cannot affect main shell'
(alias a='echo BCD')
a
__IN__
ABC
__OUT__

test_O -d -e n 'printing undefined alias is error'
unalias -a
alias a
__IN__

test_O -d -e n 'removing undefined alias is error'
alias true=false
unalias true
unalias true
__IN__

test_oE 'using alias in pipeline'
alias c=cat
! a | c | c
__IN__
ABC
__OUT__

test_oE 'using aliases in compound commands'
alias begin={ end=}
if true; then begin a; end; fi
__IN__
ABC
__OUT__

test_oE 'IO_NUMBER cannot be aliased'
alias 3=:
3>/dev/null echo \>
3</dev/null echo \<
__IN__
>
<
__OUT__

test_oE 'alias ending with blank'
alias c=cat e='echo '
e c c cat
alias c='cat '
e c c cat
__IN__
cat c cat
cat cat cat
__OUT__

test_oE 'aliases cannot substitute reserved words'
alias if=: then=: else=: fi=: for=: in=: do=: done=:
if true; then echo then; else echo else; fi
for a in A; do echo $a; done
__IN__
then
A
__OUT__

test_oE 'quoted aliases are not substituted'
alias echo=:
\echo backslash at head
ech\o backslash at tail
e'c'ho partial single-quotation
'echo' full single-quotation
e"c"ho partial double-quotation
"echo" full double-quotation
__IN__
backslash at head
backslash at tail
partial single-quotation
full single-quotation
partial double-quotation
full double-quotation
__OUT__

test_oE 'characters allowed in alias name'
alias Aa0_!%,@=echo
Aa0_!%,@ ok
__IN__
ok
__OUT__

test_oE 'recursive alias'
alias echo='echo % ' e='echo echo'
e !
# e !
# echo echo !
# echo %  echo %  !
__IN__
% echo % !
__OUT__

)

# vim: set ft=sh ts=8 sts=4 sw=4 noet:

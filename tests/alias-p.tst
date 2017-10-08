# alias-p.tst: test of aliases for any POSIX-compliant shell

posix="true"
setup 'set -e'

if testee <<\END
    alias true=false
    true
END
then
    skip="true"
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

test_oE 'using alias after assignment'
alias s=sh
a=A s -c 'echo $a'
__IN__
A
__OUT__

test_OE 'using alias after redirection'
alias e=echo
>/dev/null e not_printed
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

(
setup 'alias b=" "'

test_oE 'alias substitution to blank before if'
b if true; then echo ok; fi
__IN__
ok
__OUT__

test_OE -e n 'alias substitution to blank should not change exit status'
set +e
false
b
__IN__

test_oE 'alias substitution to blank before newline'
(
echo ok | b
cat
) </dev/null
__IN__
ok
__OUT__

)

test_oE 'alias substitution to assignment'
alias a='a=A'
a b=B sh -c 'echo $a $b'
__IN__
A B
__OUT__

test_oE 'alias substitution to redirection'
alias r='>/dev/null'
r echo not_printed
echo ok
__IN__
ok
__OUT__

test_oE 'alias substitution to here-document'
alias c='cat <<\END' d='c
here-document
END'
d
__IN__
here-document
__OUT__

test_oE 'alias substitution to !'
alias e='! echo'
if e if; then echo then; else echo else; fi
__IN__
if
else
__OUT__

test_oE 'alias substitution to parenthesis'
alias l='(' r=')'
a=A
l echo subshell; a=B; r
echo $a
__IN__
subshell
A
__OUT__

test_oE 'alias substitution to if/then/elif/else/fi keywords'
alias i='if echo' t='then echo' ei='elif echo' es='else echo' f='fi </dev/null'
i if; then echo then1; elif echo elif; then echo then2; else echo else; fi
if echo if; t then1; elif echo elif; then echo then2; else echo else; fi
if false; then echo then1; ei elif; then echo then2; else echo else; fi
if false; then echo then1; elif false; then echo then2; es else; fi
if false; then echo then1; elif false; then echo then2; else echo else; f
__IN__
if
then1
if
then1
elif
then2
else
else
__OUT__

test_oE 'alias substitution to while/until/do/done keywords'
alias w='while :' u='until :' d='do echo' dn='done | cat -'
w X; true; d while; break; dn
u X; false; d until; break; dn
__IN__
while
until
__OUT__

test_oE 'alias substitution to for'
alias f='for i in 1 2; do'
f echo $i; done
__IN__
1
2
__OUT__

test_oE 'alias substitution to in (for)'
alias forx='for x ' i='in 0' in='in 0'
forx i a; do echo $x; done
forx in a; do echo $x; done
__IN__
0
a
a
__OUT__

test_oE 'alias substitution to do/done (for)'
alias forx='for x in 1; ' fory='for y in 2; do echo $y; ' d=do dn=done
forx d echo $x; dn
fory dn
__IN__
1
2
__OUT__

test_oE 'alias substitution to case/esac keywords'
alias c='case a in a) :' e='esac </dev/null | cat -' eb='echo B;; '
c X; echo A; e
c X; eb e
__IN__
A
B
__OUT__

test_oE 'alias substitution to in (case)'
alias c='case a ' i='in a) :'
c i X; echo A; esac
__IN__
A
__OUT__

test_oE 'alias substitution to case pattern'
alias c='case a in ' a=b p='(a)'
c a) echo 1-1;; a) echo 1-2;; esac
c p echo 2; esac
alias c='case a in x) ;; '
c p echo 3; esac
alias c='case a in x| '
c a) echo 4-1;; a) echo 4-2;; esac
__IN__
1-2
2
3
4-2
__OUT__

test_oE 'alias substitution to | (case)'
alias c='case a in x ' p='|a) echo A; esac'
c p
__IN__
A
__OUT__

test_oE 'alias substitution to ) (case)'
alias c='case a in a ' p=') echo A; esac'
c p
__IN__
A
__OUT__

test_oE 'alias substitution to ;;'
alias s=';;'
case a in
    (b) s
    (a) echo A
esac
__IN__
A
__OUT__

)

test_oE 'alias substitution to function definition'
alias def='f()' f='func'
def
{ echo f; }
func
__IN__
f
__OUT__

test_oE 'IO_NUMBER cannot be aliased'
alias 3=:
3>/dev/null echo \>
3</dev/null echo \<
__IN__
>
<
__OUT__

test_oE 'alias starting with blank'
alias e=' echo'
e A
__IN__
A
__OUT__

test_oE 'alias ending with blank'
alias c=cat e='echo '
e c c cat
alias c='cat '
e c c cat
alias echo='e x x ' x=.
echo echo
alias x='x . '
echo echo
__IN__
cat c cat
cat cat cat
. x echo . x
x . x . echo x . x .
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

test_oE 'line continuation in alias name'
alias eeee=echo
ee\
e\
e ok
__IN__
ok
__OUT__

test_oE 'line continuation between alias names'
alias echo='\
echo\
 ' foo='\
bar\
' bar=X
echo           \
foo
__IN__
X
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

test_oE 'alias in command substitution'
alias e=:
func() {
    alias e=echo
    echo "$(e ok)"
}
func
__IN__
ok
__OUT__

# vim: set ft=sh ts=8 sts=4 sw=4 noet:

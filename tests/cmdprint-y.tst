# cmdprint-y.tst: yash-specific test of command printing

mkfifo fifo

test_oE 'one simple command, single line'
cat fifo&
jobs
>fifo
__IN__
[1] + Running              cat fifo
__OUT__

test_oE 'one simple command, multi-line'
f() {
    echo
}
typeset -fp f
__IN__
f()
{
   echo
}
__OUT__

test_oE 'many and-or lists, ending synchronously, single line'
{
    cat fifo; exit ; foo &
    bar& ls
    ls -l;
}&
jobs
>fifo
__IN__
[1] + Running              { cat fifo; exit; foo& bar& ls; ls -l; }
__OUT__

test_oE 'many and-or lists, ending asynchronously, single line'
{
    :& true &
    cat fifo; exit
    foo &
    bar&
}&
jobs
>fifo
__IN__
[1] + Running              { :& true& cat fifo; exit; foo& bar& }
__OUT__

test_oE 'many and-or lists, multi-line'
f() {
    echo
    echo 1
    foo &
    bar &
    ls
    ls -l
}
typeset -fp f
__IN__
f()
{
   echo
   echo 1
   foo&
   bar&
   ls
   ls -l
}
__OUT__

test_oE 'many pipelines, single line'
cat fifo && : || echo not reached&
jobs
>fifo
__IN__
[1] + Running              cat fifo && : || echo not reached
__OUT__

test_oE 'many pipelines, multi-line'
f() {
    cat fifo || echo not reached && :
}
typeset -fp f
__IN__
f()
{
   cat fifo ||
   echo not reached &&
   :
}
__OUT__

test_oE 'many commands, single line'
cat fifo | cat - | cat&
jobs
>fifo
__IN__
[1] + Running              cat fifo | cat - | cat
__OUT__

test_oE 'many commands, multi-line'
f() {
    echo | cat - | cat
}
typeset -fp f
__IN__
f()
{
   echo | cat - | cat
}
__OUT__

test_oE 'negated pipeline, single line'
! cat fifo | cat - | cat&
jobs
>fifo
__IN__
[1] + Running              ! cat fifo | cat - | cat
__OUT__

test_oE 'negated pipeline, multi-line'
f() {
    ! echo | cat - | cat
}
typeset -fp f
__IN__
f()
{
   ! echo | cat - | cat
}
__OUT__

# Non-empty grouping is tests in other tests above.

test_oE 'grouping, w/o commands, single line'
{ } && cat fifo&
jobs
>fifo
__IN__
[1] + Running              { } && cat fifo
__OUT__

test_oE 'grouping, w/o commands, multi-line'
f()
{ }
typeset -fp f
__IN__
f()
{
}
__OUT__

test_oE 'subshell, w/ single command, ending synchronously, single line'
(cat fifo)&
jobs
>fifo
__IN__
[1] + Running              (cat fifo)
__OUT__

test_oE 'subshell, w/ many commands, ending asynchronously, single line'
(cat fifo; :&)&
jobs
>fifo
__IN__
[1] + Running              (cat fifo; :&)
__OUT__

test_oE 'subshell, w/ simple command, ending synchronously, multi-line'
f() (cat fifo)
typeset -fp f
__IN__
f()
(cat fifo)
__OUT__

test_oE 'subshell, w/ many commands, ending asynchronously, multi-line'
f() (:; cat fifo; :&)
typeset -fp f
__IN__
f()
(:
   cat fifo
   :&)
__OUT__

test_oE 'subshell, w/o commands, single line'
() && cat fifo&
jobs
>fifo
__IN__
[1] + Running              () && cat fifo
__OUT__

test_oE 'if command, w/o elif, w/o else, single line'
if :& :; then cat fifo; fi&
jobs
>fifo
__IN__
[1] + Running              if :& :; then cat fifo; fi
__OUT__

test_oE 'if command, w/o elif, w/o else, multi-line'
f()
if
    :&
    :
then
    cat fifo
fi
typeset -fp f
__IN__
f()
if :&
   :
then
   cat fifo
fi
__OUT__

test_oE 'if command, w/ elif, w/o else, single line'
if :& :; then cat fifo& elif foo; then :; elif bar& then :& fi&
jobs
>fifo
__IN__
[1] + Running              if :& :; then cat fifo& elif foo; then :; elif bar& then :& fi
__OUT__

test_oE 'if command, w/ elif, w/o else, multi-line'
f()
if [ ]; then foo& elif 1; then 2; elif a& b; then c& fi
typeset -fp f
__IN__
f()
if [ ]
then
   foo&
elif 1
then
   2
elif a&
   b
then
   c&
fi
__OUT__

test_oE 'if command, w/o elif, w/ else, single line'
if :& :; then cat fifo; else echo not reached; fi&
jobs
>fifo
__IN__
[1] + Running              if :& :; then cat fifo; else echo not reached; fi
__OUT__

test_oE 'if command, w/o elif, w/ else, multi-line'
f()
if :& :; then
    cat fifo
else
    echo not reached
fi
typeset -fp f
__IN__
f()
if :&
   :
then
   cat fifo
else
   echo not reached
fi
__OUT__

test_oE 'if command, w/ elif, w/ else, single line'
if :; then cat fifo; elif foo& then :; else bar& fi&
jobs
>fifo
__IN__
[1] + Running              if :; then cat fifo; elif foo& then :; else bar& fi
__OUT__

test_oE 'if command, w/ elif, w/ else, multi-line'
f() if :; then cat fifo; elif foo& then :; else bar& fi
typeset -fp f
__IN__
f()
if :
then
   cat fifo
elif foo&
then
   :
else
   bar&
fi
__OUT__

test_oE 'if command, w/o commands, single line'
if then elif then elif then else fi && cat fifo&
jobs
>fifo
__IN__
[1] + Running              if then elif then elif then else fi && cat fifo
__OUT__

test_oE 'if command, w/o commands, multi-line'
f()
if then elif then elif then else fi
typeset -fp f
__IN__
f()
if then
elif then
elif then
else
fi
__OUT__

test_oE 'for command, w/o in, single line'
set 1
for i do cat fifo; done&
jobs
>fifo
__IN__
[1] + Running              for i do cat fifo; done
__OUT__

test_oE 'for command, w/o in, multi-line'
f()
for i do cat fifo; done
typeset -fp f
__IN__
f()
for i do
   cat fifo
done
__OUT__

test_oE 'for command, w/ in, w/o words, single line'
{ for i in; do echo not reached; done; cat fifo; }&
jobs
>fifo
__IN__
[1] + Running              { for i in; do echo not reached; done; cat fifo; }
__OUT__

test_oE 'for command, w/ in, w/o words, multi-line'
f()
for i in; do echo not reached; done
typeset -fp f
__IN__
f()
for i in
do
   echo not reached
done
__OUT__

test_oE 'for command, w/ in, w/ many words, single line'
for i in 1 2 3; do cat fifo; :; :& done&
jobs
for i in 1 2 3; do >fifo; done
__IN__
[1] + Running              for i in 1 2 3; do cat fifo; :; :& done
__OUT__

test_oE 'for command, w/ in, w/ many words, multi-line'
f()
for i in 1 2 3; do cat fifo; :; :& done
typeset -fp f
__IN__
f()
for i in 1 2 3
do
   cat fifo
   :
   :&
done
__OUT__

test_oE 'for command, w/ commands, single line'
for i do done && cat fifo&
jobs
>fifo
__IN__
[1] + Running              for i do done && cat fifo
__OUT__

test_oE 'for command, w/ commands, multi-line'
f()
for i do done
typeset -fp f
__IN__
f()
for i do
done
__OUT__

test_oE 'while command, w/ single command condition, single line'
while :; do cat fifo; break; done&
jobs
>fifo
__IN__
[1] + Running              while :; do cat fifo; break; done
__OUT__

test_oE 'while command, w/ single command condition, single line'
f()
while :; do foo; done
typeset -fp f
__IN__
f()
while :
do
   foo
done
__OUT__

test_oE 'while command, w/ many command condition, single line'
while cat fifo; break; :& do foo; bar& done&
jobs
>fifo
__IN__
[1] + Running              while cat fifo; break; :& do foo; bar& done
__OUT__

test_oE 'while command, w/ many command condition, multi-line'
f()
while cat fifo; break; :& do foo; bar& done
typeset -fp f
__IN__
f()
while cat fifo
   break
   :&
do
   foo
   bar&
done
__OUT__

test_oE 'while command, w/o commands, single line'
cat fifo || exit || while do done&
jobs
>fifo
__IN__
[1] + Running              cat fifo || exit || while do done
__OUT__

test_oE 'while command, w/o commands, multi-line'
f()
while do done
typeset -fp f
__IN__
f()
while do
done
__OUT__

test_oE 'case command, w/o case items, single line'
case i in esac && cat fifo&
jobs
>fifo
__IN__
[1] + Running              case i in esac && cat fifo
__OUT__

test_oE 'case command, w/o case items, multi-line'
f()
case i in esac
typeset -fp f
__IN__
f()
case i in
esac
__OUT__

test_oE 'case command, w/ case items, single line'
case i in (i) cat fifo;; (j) foo& bar;; (k|l|m) ;; (n) :& esac&
jobs
>fifo
__IN__
[1] + Running              case i in (i) cat fifo ;; (j) foo& bar ;; (k | l | m) ;; (n) :& ;; esac
__OUT__

test_oE 'case command, w/ case items, multi-line'
f()
case i in (i) cat fifo;; (j) foo& bar;; (k|l|m) ;; (n) :& esac
typeset -fp f
__IN__
f()
case i in
   (i)
      cat fifo
      ;;
   (j)
      foo&
      bar
      ;;
   (k | l | m)
      ;;
   (n)
      :&
      ;;
esac
__OUT__

test_oE 'double bracket, string primary'
f()
[[ "foo" ]]
typeset -fp f
__IN__
f()
[[ "foo" ]]
__OUT__

test_oE 'double bracket, unary/binary primaries'
f()
[[ -n foo || 0 -eq '1' || a = a || x < y ]]
typeset -fp f
__IN__
f()
[[ -n foo || 0 -eq '1' || a = a || x < y ]]
__OUT__

test_oE 'double bracket, disjunction in disjunction'
f()
[[ (a || b) || (c || d) ]]
typeset -fp f
__IN__
f()
[[ a || b || c || d ]]
__OUT__

test_oE 'double bracket, conjunction in disjunction'
f()
[[ (a && b) || (c && d) ]]
typeset -fp f
__IN__
f()
[[ a && b || c && d ]]
__OUT__

test_oE 'double bracket, negation in disjunction'
f()
[[ (! a) || (! b) ]]
typeset -fp f
__IN__
f()
[[ ! a || ! b ]]
__OUT__

test_oE 'double bracket, disjunction in conjunction'
f()
[[ (a || b) && (c || d) ]]
typeset -fp f
__IN__
f()
[[ ( a || b ) && ( c || d ) ]]
__OUT__

test_oE 'double bracket, conjunction in conjunction'
f()
[[ (a && b) && (c && d) ]]
typeset -fp f
__IN__
f()
[[ a && b && c && d ]]
__OUT__

test_oE 'double bracket, negation in conjunction'
f()
[[ (! a) && (! b) ]]
typeset -fp f
__IN__
f()
[[ ! a && ! b ]]
__OUT__

test_oE 'double bracket, disjunction in negation'
f()
[[ ! (a || b) ]]
typeset -fp f
__IN__
f()
[[ ! ( a || b ) ]]
__OUT__

test_oE 'double bracket, conjunction in negation'
f()
[[ ! (a && b) ]]
typeset -fp f
__IN__
f()
[[ ! ( a && b ) ]]
__OUT__

test_oE 'double bracket, negation in negation'
f()
[[ ! (! a) ]]
typeset -fp f
__IN__
f()
[[ ! ! a ]]
__OUT__

test_oE 'function definition, POSIX name, single line'
f() { :; } >/dev/null && cat fifo&
jobs
>fifo
__IN__
[1] + Running              f() { :; } 1>/dev/null && cat fifo
__OUT__

# POSIX-name multi-line function definition is tested in all other multi-line
# test cases.

test_oE 'function definition, non-POSIX name, single line'
function "${a-f}" { :; } && cat fifo&
jobs
>fifo
__IN__
[1] + Running              function "${a-f}"() { :; } && cat fifo
__OUT__

test_oE 'function definition, non-POSIX name, multi-line'
f() {
    function "${a-f}" { :; }
}
typeset -fp f
__IN__
f()
{
   function "${a-f}"()
   {
      :
   }
}
__OUT__

test_oE 'scalar assignment'
f() { foo= bar=BAR; }
typeset -fp f
__IN__
f()
{
   foo= bar=BAR
}
__OUT__

test_oE 'array assignment'
f() { foo=() bar=(1 $2 3); }
typeset -fp f
__IN__
f()
{
   foo=() bar=(1 ${2} 3)
}
__OUT__

test_oE 'single-line redirections'
f() { <f >g 2>|h 10>>i <>j <&1 >&2 >>|"3" <<<here\ string; }
typeset -fp f
__IN__
f()
{
   0<f 1>g 2>|h 10>>i 0<>j 0<&1 1>&2 1>>|"3" 0<<<here\ string
}
__OUT__

test_oE 'here-documents, single line'
cat fifo <<END 4<<\END <<-EOF&
$1
END
$1
END
		    foo
	EOF
jobs
>fifo
__IN__
[1] + Running              cat fifo 0<<END 4<<\END 0<<-EOF
__OUT__

test_oE 'here-documents, multi-line'
f() { <<END 4<<\END <<-EOF; }
$1
END
$1
END
		    foo
	EOF
typeset -fp f
__IN__
f()
{
   0<<END 4<<\END 0<<-EOF
${1}
END
$1
END
    foo
EOF
}
__OUT__

test_oE 'process redirection'
f() { <(
    :&
    echo foo
    ) >(
    :&
    echo bar
    ); }
typeset -fp f
__IN__
f()
{
   0<(:&
      echo foo) 1>(:&
      echo bar)
}
__OUT__

test_oE 'complex simple command'
f() { >/dev/null v=0 3</dev/null echo 2>/dev/null : <(); }
typeset -fp f
__IN__
f()
{
   v=0 echo : 1>/dev/null 3</dev/null 2>/dev/null 0<()
}
__OUT__

test_oE 'word w/ expansions'
f() { echo ~/"$1"/$2/${foo}/$((1 + $3))/$(echo 5)/`echo 6`; }
typeset -fp f
__IN__
f()
{
   echo ~/"${1}"/${2}/${foo}/$((1 + ${3}))/$(echo 5)/$(echo 6)
}
__OUT__

test_oE 'parameter expansion, #-prefixed'
f() { echo "${#3}"; }
typeset -fp f
__IN__
f()
{
   echo "${#3}"
}
__OUT__

test_oE 'parameter expansion, nested'
f() { echo "${{#3}-unset}"; }
typeset -fp f
__IN__
f()
{
   echo "${${#3}-unset}"
}
__OUT__

test_oE 'parameter expansion, indexed'
f() { echo "${foo[1]}${bar[2,$((1+2))]}"; }
typeset -fp f
__IN__
f()
{
   echo "${foo[1]}${bar[2,$((1+2))]}"
}
__OUT__

test_oE 'parameter expansion, w/ basic modifier'
f() { echo "${foo:+1}${bar-2}${baz:=3}${xxx?4}"; }
typeset -fp f
__IN__
f()
{
   echo "${foo:+1}${bar-2}${baz:=3}${xxx?4}"
}
__OUT__

test_oE 'parameter expansion, w/ matching'
f() { echo "${foo#x}${bar##y}${baz%z}${xxx%%0}"; }
typeset -fp f
__IN__
f()
{
   echo "${foo#x}${bar##y}${baz%z}${xxx%%0}"
}
__OUT__

test_oE 'parameter expansion, w/ substitution'
f() { echo "${a/x}${b//y}${c/#z/Z}${d/%0/0}"; }
typeset -fp f
__IN__
f()
{
   echo "${a/x/}${b//y/}${c/#z/Z}${d/%0/0}"
}
__OUT__

test_oE 'command substitution starting with subshell'
f() { echo "$((foo);)"; }
typeset -fp f
__IN__
f()
{
   echo "$( (foo))"
}
__OUT__

test_oE 'backquoted command substitution'
f() { echo "`echo \`echo foo\``"; }
typeset -fp f
__IN__
f()
{
   echo "$(echo `echo foo`)"
}
__OUT__

test_oE 'complex indentation of compound commands'
f() {
    (
    if :; foo; then
	for i in 1 2 3; do
	    while :; foo& do
		case i in
		    (i)
			[[ foo ]]
			f() {
			    cat - /dev/null <<-END
			$(here; document)
			END
			    echo ${foo-$((1 + $(bar; baz)))}
			    cat <(foo; bar <<-END
			END
			    ) >/dev/null
			}
		esac
	    done
	    until :; bar& do
		:
	    done
	done
    elif :; bar& then
	:
    else
	baz
    fi
    )
}
typeset -fp f
__IN__
f()
{
   (if :
         foo
      then
         for i in 1 2 3
         do
            while :
               foo&
            do
               case i in
                  (i)
                     [[ foo ]]
                     f()
                     {
                        cat - /dev/null 0<<-END
$(here
   document)
END
                        echo ${foo-$((1 + $(bar
                           baz)))}
                        cat 0<(foo
                           bar 0<<-END
END
                           ) 1>/dev/null
                     }
                     ;;
               esac
            done
            until :
               bar&
            do
               :
            done
         done
      elif :
         bar&
      then
         :
      else
         baz
      fi)
}
__OUT__

test_oE 'nested here-documents and command substitutions, single line'
cat fifo <<END1 <<END2&
 $(<<EOF11; <<EOF12
foo
EOF11
bar
EOF12
)
END1
 $(<<EOF21; <<EOF22
foo
EOF21
bar
EOF22
)
END2
jobs
>fifo
__IN__
[1] + Running              cat fifo 0<<END1 0<<END2
__OUT__

test_oE 'nested here-documents and command substitutions, multi-line'
f()
{
    <<END1 <<END2
 $(<<EOF11; <<EOF12
foo
EOF11
bar
EOF12
)
END1
 $(<<EOF21; <<EOF22
foo
EOF21
bar
EOF22
)
END2
}
typeset -fp f
__IN__
f()
{
   0<<END1 0<<END2
 $(0<<EOF11
foo
EOF11
   0<<EOF12
bar
EOF12
   )
END1
 $(0<<EOF21
foo
EOF21
   0<<EOF22
bar
EOF22
   )
END2
}
__OUT__

# vim: set ft=sh ts=8 sts=4 sw=4 noet:

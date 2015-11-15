# redir-y.tst: yash-specific test of redirections

echo in0 >in0
echo in1 >in1
echo 'in*' >'in*'

echo PS1= >yashrc

test_oE -e 0 \
    'pathname expansion in redirection operand (non-POSIX, interactive), success'\
    -i +m --rcfile=yashrc
cat <i*0
__IN__
in0
__OUT__

test_O -d -e 2 \
    'pathname expansion in redirection operand (non-POSIX, interactive), failure' \
    -i +m --rcfile=yashrc
cat <in*
__IN__

(
posix="true"
export ENV=yashrc

test_oE -e 0 'pathname expansion in redirection operand (POSIX, interactive)' \
    -i +m
cat <i*0
cat <in* # multiple matches: the pattern is left intact
__IN__
in0
in*
__OUT__

)

test_oE -e 0 'tilde expansion not performed in here-document operand'
HOME=/home
cat <<~
/home
~
__IN__
/home
__OUT__

test_oE -e 0 'parameter expansion not performed in here-document operand'
foo=FOO
cat <<$foo
FOO
$foo
__IN__
FOO
__OUT__

test_oE -e 0 'command substitution not performed in here-document operand'
cat <<$(echo foo)`echo bar`
foobar
$(echo foo)`echo bar`
__IN__
foobar
__OUT__

test_oE -e 0 'arithmetic expansion not performed in here-document operand'
cat <<$((1+1))
foo
$((1+1))
__IN__
foo
__OUT__

test_oE -e 0 'duplicating input to the same file descriptor'
echo foo | cat <&0
__IN__
foo
__OUT__

test_oE -e 0 'duplicating output to the same file descriptor'
echo foo >&1
__IN__
foo
__OUT__

test_Oe -e 2 'missing target for <'
<
__IN__
syntax error: the redirection target is missing
__ERR__

test_Oe -e 2 'missing target for <>'
<>
__IN__
syntax error: the redirection target is missing
__ERR__

test_Oe -e 2 'missing target for <& (EOF)'
<&
__IN__
syntax error: the redirection target is missing
__ERR__

test_Oe -e 2 'missing target for <& (line continuation and comment)'
<&\
    #
__IN__
syntax error: the redirection target is missing
__ERR__

test_Oe -e 2 'missing target for >'
>
__IN__
syntax error: the redirection target is missing
__ERR__

test_Oe -e 2 'missing target for >|'
>|
__IN__
syntax error: the redirection target is missing
__ERR__

test_Oe -e 2 'missing target for >&'
>&
__IN__
syntax error: the redirection target is missing
__ERR__

test_Oe -e 2 'missing target for >>'
>>
__IN__
syntax error: the redirection target is missing
__ERR__

test_Oe -e 2 'missing target for >>|'
>>|
__IN__
syntax error: the redirection target is missing
__ERR__

test_Oe -e 2 'missing target for <<<'
<<<
__IN__
syntax error: the redirection target is missing
__ERR__

test_Oe -e 2 'missing delimiter after <<'
<<
__IN__
syntax error: the end-of-here-document indicator is missing
__ERR__

test_Oe -e 2 'missing delimiter after <<-'
<<-
__IN__
syntax error: the end-of-here-document indicator is missing
__ERR__

test_Oe -e 2 'newline in end-of-here-document indicator'
<<'
'
__IN__
syntax error: the end-of-here-document indicator contains a newline
__ERR__

test_oE -e 0 'missing here-document delimiter'
cat <<END
foo
__IN__
foo
__OUT__

# vim: set ft=sh ts=8 sts=4 sw=4 noet:

# redir-y.tst: yash-specific test of redirections

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

# vim: set ft=sh ts=8 sts=4 sw=4 noet:

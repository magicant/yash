# param-p.tst: test of parameter expansion for any POSIX-compliant shell

posix="true"

> file
setup -d

test_oE 'format for parameter expansion'
a=a
bracket -${a-\}}- -${a-'}'}- -${a-"}"}- -${a}}-
bracket -${a-${a}}- -${a-$({ :;})}-
bracket -${a-$(($({ :;})))}- -${a-${a-${a-${a}}}}-
__IN__
[-a-][-a-][-a-][-a}-]
[-a-][-a-]
[-a-][-a-]
__OUT__

test_oE -e 0 'simplest expansion'
a=value
unset b
bracket -${a}-
bracket -${b}-
__IN__
[-value-]
[--]
__OUT__

test_oE 'expansion w/o braces, variables'
aa=value _L0NG_variable_name=x
unset aaa
bracket -$aa-
bracket -$aaa-
bracket -$_L0NG_variable_name-
__IN__
[-value-]
[--]
[-x-]
__OUT__

test_oE 'expansion w/o braces, positional parameters'
set a b
bracket -$1- $22
__IN__
[-a-][b2]
__OUT__

test_oE 'expansion w/o braces, special parameters'
set 1
: dummy&
zero="$0" dollar="$$" hyphen="$-" ex="$!" hash="$#" star="$*" at="$@"
echoraw "$??"
[ "$00"  = "${zero}0"       ] || echoraw '$0' [ "$00"  = "${zero}0"       ]
[ "$$$$" = "$dollar$dollar" ] || echoraw '$$' [ "$$$$" = "$dollar$dollar" ]
[ "$--"  = "$hyphen-"       ] || echoraw '$-' [ "$--"  = "$hyphen-"       ]
[ "$!!"  = "$ex!"           ] || echoraw '$!' [ "$!!"  = "$ex!"           ]
[ "$##"  = "$hash#"         ] || echoraw '$#' [ "$##"  = "$hash#"         ]
[ "$**"  = "$star*"         ] || echoraw '$*' [ "$**"  = "$star*"         ]
[ "$@@"  = "$at@"           ] || echoraw '$@' [ "$@@"  = "$at@"           ]
__IN__
0?
__OUT__

test_oE 'double-quoted expansion is not subject to pathname expansion'
a='*'
bracket "${a}"
bracket ${a+"${a}"}
__IN__
[*]
[*]
__OUT__

test_oE 'double-quoted expansion is not subject to field splitting'
a='a b  c'
bracket "${a}"
bracket ${a+"${a}"}
__IN__
[a b  c]
[a b  c]
__OUT__

test_oE 'tilde expansion in embedded word'
HOME=/foo/bar
bracket ${a-~} ${a-~/}
__IN__
[/foo/bar][/foo/bar/]
__OUT__

test_oE 'parameter expansion in embedded word'
b=b
bracket ${a-x${b}x}
__IN__
[xbx]
__OUT__

test_oE 'command substitution in embedded word'
bracket ${a-x$(echo -)x}
__IN__
[x-x]
__OUT__

test_oE 'arithmetic expansion in embedded word'
bracket ${a-x$((1+1))x}
__IN__
[x2x]
__OUT__

test_oE 'embedded word is expanded only if needed'
a=a
unset b
bracket -${a-${b?}}- -${b+${b?}}- -${a=${b?}}- -${a?${b?}}-
__IN__
[-a-][--][-a-][-a-]
__OUT__

test_oE 'end of embedded word'
a=a
bracket ${a-x}b}
__IN__
[ab}]
__OUT__

(
setup 'a=a n=; unset u'

test_oE '${a-b}'
bracket "${a-x}" "${n-x}" "${u-x}"
bracket "${a:-x}" "${n:-x}" "${u:-x}"
__IN__
[a][][x]
[a][x][x]
__OUT__

test_oE '${a+b}'
bracket "${a+x}" "${n+x}" "${u+x}"
bracket "${a:+x}" "${n:+x}" "${u:+x}"
__IN__
[x][x][]
[x][][]
__OUT__

test_oE '${a=b}'
bracket "${a=x}" "${n=x}" "${u=x}"
bracket "${a}" "${n}" "${u}"
__IN__
[a][][x]
[a][][x]
__OUT__

test_oE '${a:=b}'
bracket "${a:=x}" "${n:=x}" "${u:=x}"
bracket "${a}" "${n}" "${u}"
__IN__
[a][x][x]
[a][x][x]
__OUT__

test_O -d -e n 'assigning to read-only variable'
readonly n
bracket ${n:=}
__IN__

test_O -d -e n 'assigning to positional parameter'
bracket ${1:=}
__IN__

test_O -d -e n 'assigning to special parameter'
bracket ${*:=}
__IN__

test_oE '${a?b}, success'
bracket "${a?x}" "${n?x}"
bracket "${a:?x}"
__IN__
[a][]
[a]
__OUT__

test_O -d -e n '${unset?b}, failure w/o message'
bracket "${u?}"
__IN__

test_O -e n '${unset?b}, failure with message, exit status and stdout'
bracket "${u?foo bar  baz}"
__IN__

test_OE -e 0 '${unset?b}, failure with message, stderr'
(: "${u?foo bar  baz}") 2>&1 | grep -Fq 'foo bar  baz'
__IN__

test_O -d -e n '${null?b}, failure'
bracket "${n:?}"
__IN__

test_O -d -e n '${unset?b}, failure'
bracket "${u:?}"
__IN__

)

test_oE 'length of valid variables'
zero= one=a two=bb five=ccccc twenty=dddddddddddddddddddd
bracket ${#zero} ${#one} ${#five} ${#twenty}
set '' a bb cccc
bracket ${#1} ${#2} ${#3} ${#4}
: dummy&
zero="$0" dollar="$$" hyphen="$-" ex="$!" hash="$#"
echoraw '${#?}' ${#?}
[ "${#0}" = "${#zero}"   ] || echoraw '$0' [ "${#0}" = "${#zero}"   ]
[ "${#$}" = "${#dollar}" ] || echoraw '$$' [ "${#$}" = "${#dollar}" ]
[ "${#-}" = "${#hyphen}" ] || echoraw '$-' [ "${#-}" = "${#hyphen}" ]
[ "${#!}" = "${#ex}"     ] || echoraw '$!' [ "${#!}" = "${#ex}"     ]
# ambiguous...
# [ "${##}" = "${#hash}"   ] || echoraw '$#' [ "${##}" = "${#hash}"   ]
__IN__
[0][1][5][20]
[0][1][2][4]
${#?} 1
__OUT__

test_oE -e 0 'length of unset variables, success'
unset u
echoraw ${#u}
__IN__
0
__OUT__

test_O -d -e n 'length of unset variables, failure' -u
unset u
echoraw ${#u}
__IN__

test_oE 'disambiguation of ${#...'
bracket ${#-""}
bracket ${#?X}
bracket ${#+""} "${#:+}"
bracket ${#=""} "${#:=}"
__IN__
[0]
[0]
[][]
[0][0]
__OUT__

test_oE 'removing shortest matching prefix'
a=1-2-3-4 s='***' h='###'
bracket "${a#1}" "${a#*1}" "${a#1*}" "${a#1*-}"
bracket "${a#*-}" "${a#*}" "${a#-*}" "${a#*-*}"
bracket "${a#2}" "${s#'*'}" "${h#'#'}"
__IN__
[-2-3-4][-2-3-4][-2-3-4][2-3-4]
[2-3-4][1-2-3-4][1-2-3-4][2-3-4]
[1-2-3-4][**][##]
__OUT__

test_oE 'removing longest matching prefix'
a=1-2-3-4 s='***' h='###'
bracket "${a##1}" "${a##*1}" "${a##1*}" "${a##1*-}"
bracket "${a##*-}" "${a##*}" "${a##-*}" "${a##*-*}"
bracket "${a##2}" "${s##'*'}" "${h###}"
__IN__
[-2-3-4][-2-3-4][][4]
[4][][1-2-3-4][]
[1-2-3-4][**][##]
__OUT__

test_oE 'removing shortest matching suffix'
a=1-2-3-4 s='***' p='%%%'
bracket "${a%4}" "${a%*4}" "${a%4*}" "${a%-*4}"
bracket "${a%*-}" "${a%*}" "${a%-*}" "${a%*-*}"
bracket "${a%3}" "${s%'*'}" "${p%'%'}"
__IN__
[1-2-3-][1-2-3-][1-2-3-][1-2-3]
[1-2-3-4][1-2-3-4][1-2-3][1-2-3]
[1-2-3-4][**][%%]
__OUT__

test_oE 'removing longest matching suffix'
a=1-2-3-4 s='***' p='%%%'
bracket "${a%%4}" "${a%%*4}" "${a%%4*}" "${a%%-*4}"
bracket "${a%%*-}" "${a%%*}" "${a%%-*}" "${a%%*-*}"
bracket "${a%%3}" "${s%%'*'}" "${p%%%}"
__IN__
[1-2-3-][][1-2-3-][1]
[1-2-3-4][][1][]
[1-2-3-4][**][%%]
__OUT__

### Examples from informative sections of POSIX

test_oE 'effects of omitting braces'
a=1
set 2
echo ${a}b-$ab-${1}0-${10}-$10
__IN__
1b--20--20
__OUT__

test_oE 'testing existence of positional parameter'
set a b c
echo ${3:+posix}
echo ${4-posix}
__IN__
posix
posix
__OUT__

test_oE 'removing prefix with expanded word'
HOME=/home/foo
x=$HOME/src/cmd
echo ${x#$HOME}
__IN__
/src/cmd
__OUT__

test_oE 'special parameter #'
echo $#
set x
echo $#
set x 'y  y' z
echo $#
set a b c d e f g h i j k
echo $#
__IN__
0
1
3
11
__OUT__

test_oE 'special parameter ?'
true
echo $?
(exit 1)
echo $?
(exit 123)
echo $?
__IN__
0
1
123
__OUT__

test_OE -e 0 'special parameter -' -eu
set +C
v=$-
[ "$(echo $v | grep e | grep u | grep -v C)" ]
__IN__

test_OE 'special parameter $'
[ $$ -eq "$(echo $$)" ] || echo [ $$ -eq "$(echo $$)" ]
kill $$
echo not reached
__IN__

test_oE 'special parameter !'
mkfifo fifo
echo foo | {
    trap 'echo trapped; exit 0' USR1
    cat
    cat fifo
}&
exec 3>fifo
echo bar >&3
kill -s USR1 $! # should kill the last process of the background pipeline
exec 3>&-
wait $!
__IN__
foo
bar
trapped
__OUT__

# Special parameter 0 is tested in sh-p.tst
#test_oE 'special parameter 0'

test_oE 'special parameter *, quoted, unset IFS'
unset IFS
bracket "$*"
set a
bracket "$*"
set a 'b  b' cc
bracket "$*"
set ''
bracket "$*"
set '' ''
bracket "$*"
__IN__
[]
[a]
[a b  b cc]
[]
[ ]
__OUT__

test_oE 'special parameter *, quoted, non-default IFS'
IFS=xyz
bracket "$*"
set a
bracket "$*"
set a 'b  b' cc
bracket "$*"
set ''
bracket "$*"
set '' ''
bracket "$*"
__IN__
[]
[a]
[axb  bxcc]
[]
[x]
__OUT__

test_oE 'special parameter *, quoted, empty IFS'
IFS=
bracket "$*"
set a
bracket "$*"
set a 'b  b' cc
bracket "$*"
set ''
bracket "$*"
set '' ''
bracket "$*"
__IN__
[]
[a]
[ab  bcc]
[]
[]
__OUT__

test_oE 'special parameter *, unquoted'
bracket $*
set a
bracket $*
set a 'b  b' cc
bracket $*
IFS=
bracket $*
__IN__

[a]
[a][b][b][cc]
[a][b  b][cc]
__OUT__

test_oE 'special parameter @, quoted'
bracket "$@"
bracket "=$@="
set a
bracket "$@"
bracket "=$@="
set a 'b  b' cc
bracket "$@"
bracket "=$@="
bracket "$@$@"
set ''
bracket "$@"
set '' ''
bracket "$@"
bracket "=$@="
bracket "$@$@"
__IN__

[==]
[a]
[=a=]
[a][b  b][cc]
[=a][b  b][cc=]
[a][b  b][cca][b  b][cc]
[]
[][]
[=][=]
[][][]
__OUT__

# Expansion of unquoted $@ is the same as that of unquoted $*
test_oE 'special parameter @, unquoted'
bracket $@
set a
bracket $@
set a 'b  b' cc
bracket $@
IFS=
bracket $@
__IN__

[a]
[a][b][b][cc]
[a][b  b][cc]
__OUT__

test_oE '${1+"$@"}'
bracket ${1+"$@"}
set a
bracket ${1+"$@"}
set a 'b  b' cc
bracket ${1+"$@"}
__IN__

[a]
[a][b  b][cc]
__OUT__

# vim: set ft=sh ts=8 sts=4 sw=4 noet:

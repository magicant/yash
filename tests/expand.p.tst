# expand.p.tst: test of word expansions for any POSIX-compliant shell
# vim: set ft=sh ts=8 sts=4 sw=4 noet:

echol () for i do printf "%s\n" "$i"; done

HOME=/tmp/home

echo ===== parameter expansion =====

var=123/456/789 asterisks='*****' null= space=' '
unset unset
echo $var ${var} "$var" "${var}"
echo 1 $null 2
echo 1 "$null" 2
echo 1 $space 2
echo 1 "$space" 2
echol 1 -$null- 2
echol 1 "-$null-" 2
echol 1 -$space- 2
echol 1 "-$space-" 2
echo ${unset-"unset  variable"} and ${var+"set  variable"}
echo 1 ${null:-null variable} 2 ${null:+null variable} 3
echo 1 ${unset-} 2 "${unset-}" 3
echo 1 ${unset-""} 2 "${unset-""}" 3
echo 1 ${unset-unset  var} 2 "${unset-unset  var}" 3
echo 1 ${unset-"unset  var"} 2
echo 1 ${var+} 2 "${var+}" 3
echo 1 ${var+""} 2 "${var+""}" 3
echo 1 ${var+set  var} 2 "${var+set  var}" 3
echo 1 ${var+"set  var"} 2

echo =====

IFS=" "
set "" "" ""
echol [ "$@" ]
set 1 "" "" "" 5
echol [ "$@" ]
echol [ $@ ]
set -- "$@"
echol [ "$@" ]
set -- $@
echol [ "$@" ]

echol [ "" "" ]
echol [ '' '' ]
echol [ """" ]
echol [ '''' ]
set ""
echol [ "$@" ]
echol [ "-$@-" ]
set --
echol [ "$@" ]
echol [ "-$@-" ]

echo =====

set 1 '2  2' 3
echo $*
echo "$*"
IFS=""
echo "$*"

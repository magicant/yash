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

echo ===== command substitution =====

echo '\$x'
echo `echo '\$x'`
echo $(echo '\$x')

echo $(
echo ')'
)

echo $(
echo abc # a comment with )
)

echo $(
cat <<\eof
a here-doc with )
eof
)

cat <<\eof - $(
cat <<-\end
	/dev/null
	end
)
another here-doc
eof

echo ===== arithmetic expansion =====

x=1
echo $(( $(echo 3) + $x + "x" )) "$((\x + "x" + 'x'))"
echo prefix $((+-+-5)) $((~~10)) $((!0)) $((!1)) $((!-1))
echo multiplicatives $((20*5/7%3))
echo additives $((5+7-3))
echo shifts $((10<<3>>2))
echo comparisons $((-1< -1)) $((-1< 0)) $((-1< 1)) \
                 $(( 0< -1)) $(( 0< 0)) $(( 0< 1)) \
                 $(( 1< -1)) $(( 1< 0)) $(( 1< 1))
echo comparisons $((-1<=-1)) $((-1<=0)) $((-1<=1)) \
                 $(( 0<=-1)) $(( 0<=0)) $(( 0<=1)) \
                 $(( 1<=-1)) $(( 1<=0)) $(( 1<=1))
echo comparisons $((-1> -1)) $((-1> 0)) $((-1> 1)) \
                 $(( 0> -1)) $(( 0> 0)) $(( 0> 1)) \
                 $(( 1> -1)) $(( 1> 0)) $(( 1> 1))
echo comparisons $((-1>=-1)) $((-1>=0)) $((-1>=1)) \
                 $(( 0>=-1)) $(( 0>=0)) $(( 0>=1)) \
                 $(( 1>=-1)) $(( 1>=0)) $(( 1>=1))
echo equalities  $((-1==-1)) $((-1==0)) $((-1==1)) \
                 $(( 0==-1)) $(( 0==0)) $(( 0==1)) \
                 $(( 1==-1)) $(( 1==0)) $(( 1==1))
echo equalities  $((-1!=-1)) $((-1!=0)) $((-1!=1)) \
                 $(( 0!=-1)) $(( 0!=0)) $(( 0!=1)) \
                 $(( 1!=-1)) $(( 1!=0)) $(( 1!=1))
echo logicals $((0&&0)) $((0&&2)) $((2&&0)) $((2&&2))
echo logicals $((0||0)) $((0||2)) $((2||0)) $((2||2))
echo logicals $((2&&(x=2))) $((0&&(x+=1))) $((x))
echo logicals $((0||(x=1))) $((2||(x+=1))) $((x))
echo conditionals $((1?2:3)) $((0?1:2)) $((1?9:(x=0))) $((0?x=0:9)) $((x))
echo assignments $((x=0)) $((x+=5)) $((x-=2)) $((x*=8)) $((x/=6)) \
    $((x<<=2)) $((x>>=1)) $((x|=7)) $((x^=5)) $((x&=3)) $((x))
echo parentheses $(((x))) $((-(x))) $(((1+2)*3))
echo mul-add $((2*3+24/6-19%10))
echo add-shift $((2+3<<4)) $((50-2>>4))
echo shift-comp $((5<3<<1)) $((16>>1>10))
echo comp-equal $((1>2==3>=4)) $((1<2!=3<=4))
echo equal-and $((15&0==0)) $((0==0&15))
echo and-xor $((1&2^15)) $((15^2&1))
echo xor-or $((1^1|1)) $((1|1^1))
echo or-land $((3|0&&1)) $((1&&0|3))
echo land-lor $((0&&0||1)) $((1||0&&0))
echo lor-cond-assign $((0||1?2||0:0||0)) $((0||0?2||0:0||0)) \
    $((0||1?1||0?x=2?3:4:5:6)) $((x)) $((0?y:x=1)) $((x)) \
    $((x=0?1:2?3:4?5:6)) $((x))
echo $(((1*(2+3)<<(x&&x||0/0))))
unset unset
$INVOKE $TESTEE -c 'echo unset-var $((0+unset+0)) $((unset))'

echo ===== field splitting =====

unset foo bar IFS
echo +${foo-1+2}+${bar-3+4}+
echo +${foo-1 2 +3}+${bar-4+ 5+ +6}+

IFS=" +"
echo +${foo-1+2}+${bar-3+4}+
echo +${foo-1 2 +3}+${bar-4+ 5+ +6}+

set $foo bar '' xyz ''$foo'' abc
for i do echo "-$i-"; done

echol '<<' ${foo-\a \b \c} ${bar-\\d \\e \\f} '>>'

IFS=/
echo ~
echo ${foo-~}

IFS='\'
bar='1\2\3'
echol ${foo-1\\\\2'\'3"\\"4}
echol "${foo-1\\2}"
echol [ $bar ]

IFS=,
echol ${foo-4,5\,6}
echol "${foo-4,5\,6}"

IFS=0
echol $(echo 'command0subst')
echol $((99+2))

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

# variable.p.tst: test of variables for any POSIX-compliant shell
# vim: set ft=sh ts=8 sts=4 sw=4 noet:

var=xyz

echo 1 \$0=$0
$INVOKE $TESTEE <<\END
echo 2 \$0=$0
END
$INVOKE $TESTEE -c 'echo 3 \$0=$0'
$INVOKE $TESTEE -c 'echo 4 \$0=$0' 'command name'

echo ===== 4 =====

$INVOKE $TESTEE -c 'var=1 trap "echo b \$var" EXIT
    var=2 echo a $var'
$INVOKE $TESTEE -c 'var=1 trap "echo d \$var" EXIT
    echovar() { echo c $var; }
    var=2 echovar'

echo ===== set export =====

export save="$(env -i PATH="$PATH" LC_CTYPE="$LC_CTYPE" foo=123 $INVOKE $TESTEE -c 'bar=456; set')"
$INVOKE $TESTEE -c 'eval $save; echo $foo $bar'

unset save
echo - $save -
echo save ${save-unset}

export var
setvar="$(set | grep '^var=')"
var=123
echo $var
eval "$setvar"
echo $var
exportvar="$(export | grep -E '^export[[:blank:]]+var=')"
unset var
echo var ${var-unset}
eval "$exportvar"
$INVOKE $TESTEE -c 'echo $var'

unset null
export null
exportnull="$(export | grep -E '^export[[:blank:]]+null$')"
unset null
eval "$exportnull"
null=null
$INVOKE $TESTEE -c 'echo $null'

echo ===== readonly =====

readonly var ro=readonly! empty
echo $var $ro -$empty-
(var=no; echo $var) 2>/dev/null
(empty=no; echo -$empty-) 2>/dev/null
(unset var; echo $var) 2>/dev/null
(var=no sleep 0 && echo not reached - sleep) 2>/dev/null
(var=no cd && echo not reached - cd) 2>/dev/null
export readonlyvar="$(readonly | grep -E '^readonly[[:blank:]]+var=')"
$INVOKE $TESTEE -s <<\END
eval $readonlyvar
(var=no; echo $var) 2>/dev/null
END
export readonlyempty="$(readonly | grep -E '^readonly[[:blank:]]+empty$')"
$INVOKE $TESTEE -s <<\END
eval $readonlyempty
(empty=no; echo -$empty-) 2>/dev/null
END

echo ===== function unset =====

echo () { :; }
echo ng
unset -f echo
echo ok

echo ===== set shift =====

set 1 2 3 a b c
shift
echo "$@"
shift 2
echo "$@"
shift 4 2>/dev/null || echo shift 4

loop ()
while [ $# -ne 0 ]; do
    echo "$@"
    shift
done
loop x y z
echo "$@"
shift 3
echo $# "$@"

echo ===== getopts =====

echo $OPTIND

set -- -a -b "foo  bar" -c - -- -d baz
while getopts ab:c opt
do echo $opt "${OPTARG-unset}"
done
echo $opt $OPTIND

echo ===== 1
OPTIND=1
while getopts a:b opt -a -- -abc -b -- -a foo
do echo $opt "${OPTARG-unset}"
done
echo $opt $OPTIND

echo ===== 2
OPTIND=1
while getopts :abcde opt -abc -de fgh
do echo $opt "${OPTARG-unset}"
done
echo $opt $OPTIND

OPTIND=1
while getopts :abcde opt -abc -de
do echo $opt "${OPTARG-unset}"
done
echo $opt $OPTIND

echo ===== 3
set --
unset opt
OPTIND=1
! getopts "" opt
echo $? $opt $OPTIND "${OPTARG-unset}"

echo ===== 4
set -- -a b c
unset opt
OPTIND=1
getopts "" opt 2>/dev/null
echo $? $opt "${OPTARG-unset}"

set -- -a b c
unset opt
OPTIND=1
getopts : opt
echo $? $opt "${OPTARG-unset}"

set -- -a
unset opt
OPTIND=1
getopts a: opt 2>/dev/null
echo $? $opt "${OPTARG-unset}"

set -- -a
unset opt
OPTIND=1
getopts :a: opt
echo $? $opt "${OPTARG-unset}"

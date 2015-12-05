# variable.p.tst: test of variables for any POSIX-compliant shell
# vim: set ft=sh ts=8 sts=4 sw=4 noet:

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

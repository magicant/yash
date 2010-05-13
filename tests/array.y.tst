# array.y.tst: yash-specific test of the array builtin
# vim: set ft=sh ts=8 sts=4 sw=4 noet:

ary=(test of array)
array aaa 1 2 3
echo 1 $?
array
echo 2 $?
array aaa 1 2 3 4 5 6 7 8 9
echo 3 $? $aaa
array -d aaa 6 2 10 2 8
echo 4 $? $aaa
array -d aaa -1 -5 -20 0
echo 5 $? $aaa
array -i aaa 0 0
echo 6 $? $aaa
array -i aaa 2 2 3
echo 7 $? $aaa
array -i aaa 6 6
echo 8 $? $aaa
array -i aaa -1 8
echo 9 $? $aaa
array -i aaa 100 9
echo 10 $? $aaa
array -i aaa -100 !
echo 11 $? $aaa
array -s aaa 1 0
array -s aaa 2 1
array -s aaa 10 -
echo 12 $? $aaa
array -s aaa 100 x || array -s aaa -100 x || echo 13 $? $aaa

echo ===== 1 =====
echo ===== 1 ===== >&2

array --no-such-option
echo array no-such-option $?
(array >&- 2>/dev/null)
echo array output error $?

readonly aaa
array aaa
echo array readonly 1 $?
array aaa 1 2 3
echo array readonly 2 $?
array -d aaa
echo array readonly 3 $?
array -d aaa 1 2
echo array readonly 4 $?
array -i aaa 3
echo array readonly 5 $?
array -i aaa 3 1 2
echo array readonly 6 $?
array -s aaa 2 -
echo array readonly 7 $?
echo $aaa

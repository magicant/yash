tmp=/tmp/yashtest.$$

RANDOM=123
(echo $RANDOM $RANDOM $RANDOM $RANDOM $RANDOM
RANDOM=456
echo $RANDOM $RANDOM $RANDOM $RANDOM $RANDOM) >${tmp}a
(echo $RANDOM $RANDOM $RANDOM $RANDOM $RANDOM
RANDOM=456
echo $RANDOM $RANDOM $RANDOM $RANDOM $RANDOM) >${tmp}b
diff ${tmp}a ${tmp}b && echo ok

echo ===== 1 =====

func1 () { echo func1; }
func2 () { echo func2; }

func1; func2;

func1 () {
	func1 () { echo re-defined func1; }
	func2 () { echo re-defined func2; }
	echo re-defined.
}
func2; func1; func2; func1;


# TODO should be removed in EXIT trap
rm -f ${tmp}a ${tmp}b

RANDOM=123
(echo $RANDOM $RANDOM $RANDOM $RANDOM $RANDOM
RANDOM=456
echo $RANDOM $RANDOM $RANDOM $RANDOM $RANDOM) >"${TESTTMP}/variable-a"
(echo $RANDOM $RANDOM $RANDOM $RANDOM $RANDOM
RANDOM=456
echo $RANDOM $RANDOM $RANDOM $RANDOM $RANDOM) >"${TESTTMP}/variable-b"
diff "${TESTTMP}/variable-a" "${TESTTMP}/variable-b" && echo ok

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


rm -f "${TESTTMP}/variable-a" "${TESTTMP}/variable-b"

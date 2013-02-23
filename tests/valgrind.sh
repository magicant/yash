# valgrind.sh: runs tests with valgrind
# (C) 2011 magicant

cd -- "$(dirname -- "$0")" || exit

: ${MAKE:='make'}
: ${VALGRIND:='valgrind --leak-check=full --log-file=vallog.%p'}
: ${TESTEE:='../yash'}

if [ "$(find . -name 'vallog.*')" ]; then
	printf 'Error: vallog.* file exists in this directory\n' >&2
	exit 1
fi

do_test() {
	"$@"
	grep 'ERROR SUMMARY:' vallog.* | grep -v 'ERROR SUMMARY: 0 errors'
	r1=$?
	grep 'lost:' vallog.* | grep -v 'lost: 0 bytes'
	r2=$?
	if [ $r1 -eq 0 ] || [ $r2 -eq 0 ]; then exit 1; fi
	rm vallog.*
}

while getopts s:v opt; do
	case $opt in
		s)
			VALGRIND="$VALGRIND --suppressions=$OPTARG"
			;;
		v)
			VALGRIND="$VALGRIND -v --track-origins=yes"
			;;
	esac
done
shift $((OPTIND-1))

if [ $# -eq 0 ]; then
	set -- *.tst
fi
for test do
	case $test in
		*.p.tst)
			do_test $MAKE TESTEE="$VALGRIND $TESTEE --posix" TEST_ITEMS="$test"
			;;
		*.y.tst)
			do_test $MAKE TESTEE="$VALGRIND $TESTEE" TEST_ITEMS="$test"
			;;
	esac
done

printf 'No memory errors detected\n'


# builtin.y fails because $0 is not set to 'sh' and valgrind's signal handling
# job.y fails because of valgrind's signal handling
# redir.p fails because of file descriptors opened by valgrind
# variable.p fails because $0 is not set to 'sh'

# test.y.tst: yash-specific test of the test builtin
# vim: set ft=sh ts=8 sts=4 sw=4 noet:

tmp="${TESTTMP}/test.y.tmp"

mkdir "$tmp" || exit
cd "$tmp" || exit
umask u=rwx,go=
command -V test | grep -v "^test: a regular built-in "
command -V [ | grep -v "^\[: a regular built-in "

tt () {
    printf "%s: " "$*"
    test "$@"
    printf "%d " $?
    [ "$@" ]
    printf "%d\n" $?
}

echo =====

tt
tt ""
tt 1
tt --
tt -n
tt -z
tt -t
tt ! ""
tt ! 1
tt ! 000

echo =====

# skip some tests when i'm root
: ${EUID:=$(id -u)}
if [ "$EUID" -eq 0 ]; then
    isroot=true
else
    isroot=false
fi

mkfifo fifo
ln -s fifo fifolink
ln -s gid reglink
touch gid uid readable1 readable2 readable3 writable1 writable2 writable3
ln gid gidhard
mkdir sticky
chown $(id -u):$(id -g) gid
chmod g+xs gid
chmod u+xs uid
chmod =,u=r readable1
chmod =,g=r readable2
chmod =,o=r readable3
chmod =,u=w writable1
chmod =,g=w writable2
chmod =,o=w writable3
chmod +t sticky
echo "exit 0" >> executable1
cp executable1 executable2
cp executable1 executable3
chmod u+x executable1
chmod g+x executable2
chmod o+x executable3
touch -t 200001010000 older
touch -t 200101010000 newer
touch -a -t 200101010000 old; touch -m -t 200001010000 old
touch -a -t 200001010000 new; touch -m -t 200101010000 new

# check of the -b, -c, -G and -S operators are skipped
# check of the -t operator is in job.y.tst
tt -d .
tt -d fifolink
tt -e .
tt -e fifolink
tt -e no_such_file
tt -f gid
tt -f reglink
tt -f fifolink
tt -f .
tt -f no_such_file
tt -g gid
tt -g uid
tt -h fifolink
tt -h reglink
tt -h gid
tt -h no_such_file
#tt -k sticky
tt -k gid
tt -L fifolink
tt -L reglink
tt -L gid
tt -L no_such_file
tt -N new
tt -N old
tt -n ""
tt -n 0
tt -n 1
tt -n abcde
tt -O .
tt -p fifo
tt -p .
tt -r readable1
if $isroot; then cat <<END
-r readable2: 1 1
-r readable3: 1 1
-r writable1: 1 1
-r writable2: 1 1
-r writable3: 1 1
END
else
tt -r readable2
tt -r readable3
tt -r writable1
tt -r writable2
tt -r writable3
fi
tt -s gid
tt -s executable1
tt -u gid
tt -u uid
if $isroot; then cat <<END
-w readable1: 1 1
-w readable2: 1 1
-w readable3: 1 1
END
else
tt -w readable1
tt -w readable2
tt -w readable3
fi
tt -w writable1
if $isroot; then cat <<END
-w writable2: 1 1
-w writable3: 1 1
END
else
tt -w writable2
tt -w writable3
fi
tt -x .
tt -x executable1
if $isroot; then cat <<END
-x executable2: 1 1
-x executable3: 1 1
-x reglink: 1 1
END
else
tt -x executable2
tt -x executable3
tt -x reglink
fi
tt -z ""
tt -z 0
tt -z 1
tt -z abcde

echo =====

tt "" = ""
tt 1 = 1
tt abcde = abcde
tt 0 = 1
tt abcde = 12345
tt ! = !
tt = = =
tt "(" = ")"
tt "" == ""
tt 1 == 1
tt abcde == abcde
tt 0 == 1
tt abcde == 12345
tt ! == !
tt == == ==
tt "(" == ")"
tt "" != ""
tt 1 != 1
tt abcde != abcde
tt 0 != 1
tt abcde != 12345
tt ! != !
tt != != !=
tt "(" != ")"
tt a === a
tt a !== a
tt a '<' a
tt a '<=' a
tt a '>' a
tt a '>=' a
tt abc123xyz     =~ 'c[[:digit:]]*x'
tt -axyzxyzaxyz- =~ 'c[[:digit:]]*x'
tt -axyzxyzaxyz- =~ '-(a|xyz)*-'
tt abc123xyz     =~ '-(a|xyz)*-'
tt ! -n ""
tt ! -n 0
tt ! -n 1
tt ! -n abcde
tt ! -z ""
tt ! -z 0
tt ! -z 1
tt ! -z abcde
tt "(" "" ")"
tt "(" 0 ")"
tt "(" abcde ")"

echo =====

tt -3 -eq -3
tt 90 -eq 90
tt 0 -eq 0
tt -3 -eq 90
tt -3 -eq 0
tt 90 -eq 0
tt -3 -ne -3
tt 90 -ne 90
tt 0 -ne 0
tt -3 -ne 90
tt -3 -ne 0
tt 90 -ne 0
tt -3 -lt -3
tt -3 -lt 0
tt 0 -lt 90
tt 0 -lt -3
tt 90 -lt -3
tt 0 -lt 0
tt -3 -le -3
tt -3 -le 0
tt 0 -le 90
tt 0 -le -3
tt 90 -le -3
tt 0 -le 0
tt -3 -gt -3
tt -3 -gt 0
tt 0 -gt 90
tt 0 -gt -3
tt 90 -gt -3
tt 0 -gt 0
tt -3 -ge -3
tt -3 -ge 0
tt 0 -ge 90
tt 0 -ge -3
tt 90 -ge -3
tt 0 -ge 0
tt XXXXX -ot newer
tt XXXXX -ot XXXXX
tt newer -ot XXXXX
tt older -ot newer
tt newer -ot newer
tt newer -ot older
tt XXXXX -nt newer
tt XXXXX -nt XXXXX
tt newer -nt XXXXX
tt older -nt newer
tt older -nt older
tt newer -nt older
tt XXXXX -ef newer
tt XXXXX -ef XXXXX
tt newer -ef XXXXX
tt older -ef newer
tt older -ef older
tt newer -ef older
tt gid -ef gidhard
tt gid -ef reglink

echo =====

tt "" -veq ""
tt "" -vne ""
tt "" -vgt ""
tt "" -vge ""
tt "" -vlt ""
tt "" -vle ""
tt 0 -veq 0
tt 0 -vne 0
tt 0 -vgt 0
tt 0 -vge 0
tt 0 -vlt 0
tt 0 -vle 0
tt 0 -veq 1
tt 0 -vne 1
tt 0 -vgt 1
tt 0 -vge 1
tt 0 -vlt 1
tt 0 -vle 1
tt 1 -veq 0
tt 1 -vne 0
tt 1 -vgt 0
tt 1 -vge 0
tt 1 -vlt 0
tt 1 -vle 0
tt 01 -veq 0001
tt 02 -vle 0100
tt .%=01 -veq .%=0001
tt .%=02 -vle .%=0100
tt 0.01.. -veq 0.1..
tt 0.01.0 -vlt 0.1..
tt 0.01.0 -vlt 0.1.:
tt 0.01.0 -veq 0.1.
tt 0.01.0 -vle 0.1.a0
tt 1.2.3 -vle 1.3.2
tt -2 -vle -3

echo =====

tt "" -a ""
tt "" -a 1
tt 1 -a ""
tt 1 -a 1
tt "" -o ""
tt "" -o 1
tt 1 -o ""
tt 1 -o 1
tt "(" 12345 = 12345 ")"
tt "(" 12345 = abcde ")"
tt "(" "(" 12345 = 12345 ")" ")"
tt "(" "(" 12345 = abcde ")" ")"
tt 1 -a "(" 1 = 0 -o "(" 2 = 2 ")" ")" -a "(" = ")"
tt "" -a 0 -o 0  # -a has higher precedence than -o
tt 0 -o 0 -a ""  # -a has higher precedence than -o
tt ! "" -a ""    # 4-argument test: ! ( "" -a "" )
tt "(" ! "" -a "" ")"  # many-argument test: ! has higher precedence than -a
tt ! "(" "" -a "" ")"
tt "(" ! "" ")" -a ""
tt -n = -o -o -n = -n  # ( -n = -o ) -o ( -n = -n ) => true
tt -n = -a -n = -n     # ( -n = ) -a ( -n = -n )    => true

echo =====

(
set -o allexport
tt -o allexpor
tt -o allexport
tt -o allexportttttttttttttt
tt -o \?allexpor
tt -o \?allexport
tt -o \?allexportttttttttttttt
)
(
set +o allexport
tt -o allexpor
tt -o allexport
tt -o allexportttttttttttttt
tt -o \?allexpor
tt -o \?allexport
tt -o \?allexportttttttttttttt
)

echo =====

test 1 2 3  2>/dev/null  # invalid expression
echo "1 2 3: $?"


cd "${TESTTMP}"
rm -fr "$tmp"

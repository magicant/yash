# random-y.tst: test of the $RANDOM special variable

setup 'eventually() {
    round=0
    until eval "$1"; do
        round=$((round + 1))
        if [ "$round" -gt 100 ]; then
            printf "eventually: failed after 100 rounds: %s\n" "$1" >&2
            return 1
        fi
    done
}'

test_E 'RANDOM yields random numbers'
eventually '[ "$RANDOM" -ne "$RANDOM" ]'
eventually '[ "$((RANDOM % 7))" -eq 0 ]'
__IN__

test_E 'subshell RANDOM yields random numbers different from parent shell'
eventually '[ "$RANDOM" -ne "$(echo $RANDOM)" ]'
eventually '[ "$(echo $RANDOM)" -ne "$RANDOM" ]'
__IN__

test_E 'RANDOM yields different numbers in different subshells'
eventually '[ "$(echo $RANDOM)" -ne "$(echo $RANDOM)" ]'
eventually '[ "$(echo $RANDOM)" -ne "$(echo $RANDOM)" ]'
eventually '[ "$(echo $RANDOM)" -ne "$(echo $(echo $RANDOM))" ]'
eventually '[ "$(echo $(echo $RANDOM))" -ne "$(echo $RANDOM)" ]'
__IN__

test_E -e 0 'subshell RANDOM differs between shell invocations' -e
run() { "$TESTEE" -c '(echo $RANDOM); :'; }
eventually '[ "$(run)" -ne "$(run)" ]'
__IN__

test_E -e 0 'assigning same seed yields same random sequence' -e
print() {
    RANDOM=123
    echo $RANDOM $RANDOM $RANDOM $RANDOM $RANDOM
    RANDOM=456
    echo $RANDOM $RANDOM $RANDOM $RANDOM $RANDOM
}
print > seeded1
print > seeded2
diff seeded1 seeded2
__IN__

test_E -e 0 'assigning different seeds yields different random sequences' -e
print() {
    RANDOM=$1
    echo $RANDOM $RANDOM $RANDOM $RANDOM $RANDOM $RANDOM $RANDOM $RANDOM
}
test "$(print 123)" != "$(print 456)"
__IN__

test_E -e 0 'after seeding subshell yields numbers different from parent' -e
RANDOM=123
eventually '[ "$RANDOM" -ne "$(echo $RANDOM)" ]'
eventually '[ "$(echo $RANDOM)" -ne "$RANDOM" ]'
RANDOM=456
eventually '[ "$RANDOM" -ne "$(echo $RANDOM)" ]'
eventually '[ "$(echo $RANDOM)" -ne "$RANDOM" ]'
__IN__

test_E -e 0 'after seeding different subshells yield different numbers' -e
RANDOM=123
eventually '[ "$(echo $RANDOM)" -ne "$(echo $RANDOM)" ]'
eventually '[ "$(echo $RANDOM)" -ne "$(echo $RANDOM)" ]'
eventually '[ "$(echo $RANDOM)" -ne "$(echo $(echo $RANDOM))" ]'
eventually '[ "$(echo $(echo $RANDOM))" -ne "$(echo $RANDOM)" ]'
RANDOM=456
eventually '[ "$(echo $RANDOM)" -ne "$(echo $RANDOM)" ]'
eventually '[ "$(echo $RANDOM)" -ne "$(echo $RANDOM)" ]'
eventually '[ "$(echo $RANDOM)" -ne "$(echo $(echo $RANDOM))" ]'
eventually '[ "$(echo $(echo $RANDOM))" -ne "$(echo $RANDOM)" ]'
__IN__

test_E -e 0 'assigning same seed yields same random sequence in subshells' -e
print() {
    RANDOM=123
    echo $(echo $(echo $RANDOM) $RANDOM) $(echo $(echo $RANDOM) $RANDOM)
    RANDOM=456
    echo $(echo $(echo $RANDOM) $RANDOM) $(echo $(echo $RANDOM) $RANDOM)
}
print > subseeded1
print > subseeded2
diff subseeded1 subseeded2
__IN__

test_E -e 0 'same seed yields same sequence between parent and subshell' -e
print() {
    RANDOM=123
    echo $RANDOM $RANDOM $RANDOM $RANDOM $RANDOM
    RANDOM=456
    echo $RANDOM $RANDOM $RANDOM $RANDOM $RANDOM
}
print > psseeded1
(print > psseeded2)
diff psseeded1 psseeded2
__IN__

test_E -e 0 'forking subshells does not change parent sequence' -e
{
    RANDOM=123
    echo $RANDOM $RANDOM $RANDOM $RANDOM $RANDOM
    RANDOM=456
    echo $RANDOM $RANDOM $RANDOM $RANDOM $RANDOM
} > fsseeded1
{
    RANDOM=123
    echo $(:) $RANDOM $RANDOM $RANDOM $RANDOM $RANDOM
    RANDOM=456
    echo $(:) $RANDOM $RANDOM $RANDOM $RANDOM $RANDOM
} > fsseeded2
diff fsseeded1 fsseeded2
__IN__

test_E -e 0 'forking nested subshells does not change parent sequence' -e
{
    RANDOM=123
    echo $(echo $RANDOM $RANDOM $RANDOM $RANDOM $RANDOM)
    RANDOM=456
    echo $(echo $RANDOM $RANDOM $RANDOM $RANDOM $RANDOM)
} > fssseeded1
{
    RANDOM=123
    echo $(echo $(:) $RANDOM $RANDOM $RANDOM $RANDOM $RANDOM)
    RANDOM=456
    echo $(echo $(:) $RANDOM $RANDOM $RANDOM $RANDOM $RANDOM)
} > fssseeded2
diff fssseeded1 fssseeded2
__IN__

test_E -e 0 'differently derived subshells yield different numbers' -e
a=$(
    RANDOM=42
    echo $(:) $(echo $RANDOM $RANDOM $RANDOM $RANDOM $RANDOM $RANDOM)
)
b=$(
    RANDOM=42
    echo $(echo $(:) $RANDOM $RANDOM $RANDOM $RANDOM $RANDOM $RANDOM)
)
test "$a" != "$b"
__IN__

test_o 'assigning non-seed to RANDOM'
(RANDOM=; echo [$RANDOM])
(RANDOM=X; echo [$RANDOM])
__IN__
[]
[X]
__OUT__

test_o 'once unset RANDOM no longer yields random numbers'
unset RANDOM
echo ${RANDOM-unset}
RANDOM=123
echo $RANDOM $RANDOM $RANDOM $RANDOM $RANDOM
__IN__
unset
123 123 123 123 123
__OUT__

test_E 'read-only RANDOM yields random numbers'
readonly RANDOM
eventually '[ "$RANDOM" -ne "$RANDOM" ]'
__IN__

# vim: set ft=sh ts=8 sts=4 sw=4 et:

PS1= PS2=; echo >&2 #1

echo 2 #2
fc -l #3
fc -l -n #4
fc -l -r #5
echo 3 #6
fc -s #7
fc -s 3=4 #8
fc -s 2=5 2 #9
fc -l 6 #10
fc -l 7 7 #11
fc -l 9 7 #12
echo ===== \#13
fc -s -- -=+ #14
fc -s -- -2 #15
fc -rl -- -3 -5 #16
echo ===== \#17
echo ===== \#18
echo ===== \#19
fc -l #20
echo ===== \#21
fc -l 1 10000 #22
fc -l echo #23
echo ===== \#24
fc -s 21 >/dev/null #25
echo 1 #26
echo 12 #27
echo 123 #28
fc -e true -- -3 28 #29-31
FCEDIT=false #32
fc

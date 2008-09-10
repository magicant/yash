PS1= PS2= #1

echo 2 #2
fc -l #3
fc -l -n #4
fc -l -r #5
echo 3 #6
fc -s #8
fc -s 3=4 #10
fc -s 2=5 2 #12
fc -l 6 #13
fc -l 7 9 #14
fc -l 12 8 #15
echo ===== \#16
fc -s -- -=+ #18
fc -s -- -2 #20
fc -rl -- -3 -5 #21
echo ===== \#22
echo ===== \#23
echo ===== \#24
fc -l #25
echo ===== \#26
fc -l 1 10000 #27

echo >&2

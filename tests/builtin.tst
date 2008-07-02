echo ===== break continue =====

breakfunc ()
while true; do
	echo break ok
	break 999
	echo break ng
done
while true; do
	echo break ok 2
	breakfunc
	echo break ng 2
done

contfunc ()
while true; do
	echo continue ok
	continue 999
	echo continue ng
done
for i in 1 2; do
	echo continue $i
	contfunc
done


echo ===== . =====

set a b c
. ./dot.t 1 2 3
echo $count
echo -"$@"-

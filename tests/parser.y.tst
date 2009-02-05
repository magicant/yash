echo ===== for =====

# semicolon after identifier only allowed in non-posix mode
set 1 2 3
for i; do echo $i; done
for i;
do echo $i;
done

echo ===== empty compound commands =====

(exit 1)
()
(
)
{ }
{
}
echo a $?

(exit 1)
if then fi
if
then
fi
if then elif then else fi
if
then
elif
then
else
fi
echo b $?

(exit 1)
for _ in; do done
echo c1 $?
(exit 1)
for _ in
do
done
echo c2 $?
(exit 1)
for _ in 1 2 3; do done
echo c3 $?

while false; do done
echo d1 $?
false
until do done
echo d2 $?
i=3
while do
	if [ $i -eq 0 ]; then break; fi
	i=$((i-1))
	echo $i
done
until do
	echo not reached
done

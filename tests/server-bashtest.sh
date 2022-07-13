#!/bin/bash

port=8080
if (( "$#" == 1 )) && (( "$1" > 1023 )); then
	port="$1"
elif (( "$#" == 1 )); then
	echo "Warning: Port numbers less than 1024 are reserved. Defaulting to port 3010..."
elif [[ "$#" -ne 0 ]]; then
	echo "asgn0-test: Program takes up to 1 argument (port number). Exiting..."
	exit 1
fi

check_GET_Diff () {
	i=0
	while [ $i -lt $3 ]; do
		out=$(diff $1 <(timeout 5 curl -s localhost:$port/r"$(($2 + $i))".txt))

		if [ ! "$out" = "" ]; then
			printf "command: diff $1 <(curl -s localhost:$port/r$(($2 + $i)).txt)\n"
			break
		fi
		((++i))
	done
	
	if [ $? -ne 124 ]; then
		echo $out
	else
		echo timed out
	fi
}

check_HEAD_Diff () {
	i=0
	while [ $i -lt $3 ]
	do
		out=$(diff <(printf "HTTP/1.1 200 OK\r\nContent-Length: $1\r\n\r\n") <(timeout 5 curl -sI localhost:$port/r"$(($2 + $i))".txt))

		if [ ! "$out" = "" ]; then
			break
		fi
		((++i))
	done

	if [ $? -ne 124 ]; then
		echo $out
	else
		echo timed out
	fi
}

check_PUT_Diff () {
	putCalls $1 $2 $3

	iter=0
	while [ $iter -lt $3 ]; do
		out=$(diff $1 r"$(($2 + $iter))".txt)

		if [ ! "$out" = "" ]; then
			break
		fi
		((++iter))
	done

	if [ $? -ne 124 ]; then
		echo $out
	else
		echo timed out
	fi
}

putCalls () {
	for i in $(seq 0 $(($3 - 1))); do
		timeout 16 curl -sT $1 localhost:$port/r"$(($2 + $i))".txt >/dev/null &
	done
	wait
}

mixedCalls() {
	curl -sT $1 localhost:$port/$2 > /dev/null & curl -I localhost:$port/$3 >/dev/null & wget -qO - localhost:$port/$4 >/dev/null & curl -sT $1 localhost:$port/$5 
}

#################################################################################################
#### Run C Test suite written by @Burning on discord. If you do not have their test script,  ####
#### this particular suite will not execute, but I highly encourage you to get them.         ####
#### These scripts, much like the asgn0 scripts, work best in tandem!                        ####
#################################################################################################

#if [ -f "testScript.c" ] && [ "$port" = "8080" ]; then
#	echo "====Running Burning's Tests===="
#	./testScript
#fi

printf "====Cleaning up files from previous runs if they exist, and clearing log_file====\n"
NUM_IN_FILES=7
iter=$(($NUM_IN_FILES+1))
while [ $iter -le $((3*$NUM_IN_FILES)) ]; do
	if [ -f r"$iter".txt ]; then
		rm -f r"$iter".txt
	fi
	((++iter))
done
> log_file

#### Create input files ####
iter=0
if [ ! -f "r1.txt" ]; then
	touch r1.txt
	echo "Creating smaller test txt file"
	while [ $iter -le 5 ];
	do
		cat httpserver.c >> r1.txt
		((++iter))
	done
fi

if [ ! -f "r2.txt" ]; then
	touch r2.txt
	echo "Creating smaller binary test file"
	cat httpserver >> r2.txt
fi

iter=0
if [ ! -f "r3.txt" ]; then
	touch r3.txt
	echo "Creating smaller Makefile test file"
	while [ $iter -le 5 ];
	do
		cat Makefile >> r3.txt
		((++iter))
	done
fi

if [ ! -f "r4.txt" ]; then
	touch r4.txt
	echo "Downloading larger test file"
	curl -s ftp://ccg.epfl.ch/epd/current/epd.seq | tac | tac | head -qn 40000 > r4.txt
fi

iter=0
if [ ! -f "r5.txt" ]; then
	touch r5.txt
	echo "Creating second large test file"
	while [ $iter -le 13 ]
	do
		cat httpserver.c >> r5.txt
		cat Makefile >> r5.txt
		((++iter))
	done
fi

if [ ! -f "r6.txt" ]; then
	touch r6.txt
	out=$(which head)
	cat $out >> r6.txt
fi

if [ ! -f "r7.txt" ]; then
	touch r7.txt
	echo "Creating larger mixed test file"
	cat r4.txt >> r7.txt
	cat r6.txt >> r7.txt
	cat r5.txt >> r7.txt
fi

testCase=1

#### Call GET on each test file and diff it with actual file ####
#### Tests 1-7                                               ####
echo "====Running GET tests===="

iter=1
while [ $iter -le $NUM_IN_FILES ]; do
	FILE=r"$iter".txt
	out=$(check_GET_Diff $FILE $iter 1)

	printf "Test $testCase: "
	if [ "$out" = "" ]; then
		printf "PASS\n"
	else
		printf "FAIL. Difference found. Called diff between $FILE and GET on $FILE\n"
	fi

	((++iter))
	((++testCase))
done

#### Call HEAD on each test file and diff it with wc of file ####
#### Tests 8-14                                              ####
echo "====Running HEAD tests===="

iter=1
while [ $iter -le $NUM_IN_FILES ]; do
	FILE=r"$iter".txt
	WC_OUT=$(wc $FILE)
	tokens=( $WC_OUT )
	WORD_COUNT=${tokens[2]}
	out=$(check_HEAD_Diff $WORD_COUNT $iter 1)

	printf "Test $testCase: "
	if [ "$out" = "" ]; then
		printf "PASS\n"
	else
		printf "FAIL. Difference found. Called diff between $FILE and HEAD on $FILE\n"
	fi
	((++testCase))
	((++iter))
done

#### PUT file into another file, then diff the resulting files ####
#### Tests 15-21                                               ####
echo "====Running PUT tests===="

iter=1
while [ $iter -le $NUM_IN_FILES ]; do
	INFILE=r"$iter".txt
	out=$(check_PUT_Diff $INFILE 8 4)

	printf "Test $testCase: "
	if [ "$out" = "" ]; then
		printf "PASS\n"
	else
		printf "FAIL. Difference found. Calling PUT on [r8.txt, ..., r11.txt] with input file $INFILE, then diffing associated files\n"
	fi
	((++testCase))
	((++iter))
done

#### Call GET on the files created in the above loop and diff them with OG file ####
#### Test 22                                                                    ####
echo "====Running GETs on files created/updated with PUTs above===="

out=$(check_GET_Diff $INFILE 8 4)

printf "Test $testCase: "
if [ "$out" = "" ]; then
	printf "PASS\n"
else
	printf "FAIL. Difference found. Calling GET on [r8.txt, ..., r11.txt] and diffing them with $INFILE\n"
fi
((++testCase))


#### Call HEAD on each test file and diff it with wc and expected return of file ####
#### Tests 23                                                                    ####
echo "====Running HEADs on files created/updated with PUTs above===="

WC_OUT=$(wc $INFILE)
tokens=( $WC_OUT )
WORD_COUNT=${tokens[2]}
out=$(check_HEAD_Diff $WORD_COUNT 8 4)

printf "Test $testCase: "
if [ "$out" = "" ]; then
	printf "PASS\n"
else
	printf "FAIL. Difference found. Calling HEAD on [r8.txt, ..., r11.txt] and diffing them with $INFILE using wc\n"
fi

((++testCase))

#### Call put on same file twice then check output of GET ####
#### Tests 24-30                                          ####
echo "====Calling GET after truncation===="

iter=1
while [ $iter -le $NUM_IN_FILES ]
do
	INFILE0=r"$iter".txt
	INFILE1=Makefile

	putCalls $INFILE0 8 4
	putCalls $INFILE1 8 4

	out0=$(check_GET_Diff $INFILE0 8 4)
	out1=$(check_GET_Diff $INFILE1 8 4)

	printf "Test $testCase: "
	if [ ! "$out0" = "" ] && [ "$out1" = "" ]; then
		printf "PASS\n"
	else
		printf "FAIL. Difference found. Calling PUT on [r8.txt, ..., r11.txt] with file $INFILE0, then with $INFILE1, then performing diff on the associated files\n"
	fi
	((++testCase))
	((++iter))
done


#### Call put on same file twice then check output of HEAD ####
#### Tests 31-37                                           ####
echo "====Calling HEAD after truncation===="

iter=1
while [ $iter -le $NUM_IN_FILES ]
do
	OUTFILE0=Makefile
	OUTFILE1=r"$(($iter))".txt

	putCalls $OUTFILE1 8 4
	putCalls $OUTFILE0 8 4

	WC_OUT0=$(wc $OUTFILE0)
	tokens0=( $WC_OUT0 )
	WORD_COUNT0=${tokens0[2]}

	WC_OUT1=$(wc $OUTFILE1)
	tokens1=( $WC_OUT1 )
	WORD_COUNT1=${tokens1[2]}

	out0=$(check_HEAD_Diff $WORD_COUNT0 8 4)
	out1=$(check_HEAD_Diff $WORD_COUNT1 8 4)

	printf "Test $testCase: "
	if [ "$out0" = "" ] && [ ! "$out1" = "" ]; then
		printf "PASS\n"
	else
		printf "FAIL. Difference found. Calling PUT on [r8.txt, ..., r11.txt] with file $OUTFILE1, then with $OUTFILE0, then performing diff on the associated files\n"
	fi

	((++testCase))
	((++iter))
done

#### Check for invalid resource names, content-lengths, hosts among other things ####
#### Tests 38-50                                                                 ####
echo "====Running Bad Request tests===="
FILE1=r1.txt
FILE2=r2.txt

out=$(diff <(timeout 5 curl -s localhost:"$port"/"$FILE1" -H "Host: a a") <(echo Bad Request))
printf "Test $testCase: "

if [ "$out" = "" ]; then
	printf "PASS\n"
else
	printf "FAIL. Difference found. Command run: diff <(curl -s localhost:$port/$FILE -H \"Host: a a\") <(echo Bad Request)\n"
fi
((++testCase))

out=$(diff <(timeout 5 curl -sT $FILE1 localhost:"$port"/"$FILE2" -H "Host: a a") <(echo Bad Request))
printf "Test $testCase: "

if [ "$out" = "" ]; then
	printf "PASS\n"
else
	printf "FAIL. Difference found. Command run: diff <(curl -s -T r1.txt localhost:$port/$FILE -H \"Host: a a\") <(echo Bad Request)\n"
fi
((++testCase))

out=$(diff <(timeout 5 curl -sI localhost:"$port"/"$FILE1" -H "Host: a a")  <(printf "HTTP/1.1 400 Bad Request\r\nContent-Length: 12\r\n\r\n"))
printf "Test $testCase: "

if [ "$out" = "" ]; then
	printf "PASS\n"
else
	printf 'FAIL. Difference found. Command run: diff <(curl -sI localhost:%s/%s -H \"Host: a a\") <(printf \"HTTP/1.1 400 Bad Request\\r\\nContent-Length: 12\\r\\n\\r\\n\")\n' $port $FILE
fi
((++testCase))

out=$(diff <(printf "TEAPOT /$FILE1 HTTP/1.1\r\nHost: localhost:$port\r\nContent-Length: 12\r\n\r\n" | nc -C localhost "$port") <(printf 'HTTP/1.1 501 Not Implemented\r\nContent-Length: 16\r\n\r\nNot Implemented\n'))
printf "Test $testCase: "

if [ "$out" = "" ]; then
	printf "PASS\n"
else
	echo out is $out
	printf 'FAIL. Difference found. Command run: diff <(printf \"TEAPOT /%s HTTP/1.1\\r\\nHost: localhost:%s\\r\\nContent-Length: 12\\r\\n\\r\\n" | nc -C localhost %s) <(printf \"HTTP/1.1 501 Not Implemented\\r\\nContent-Length: 16\\r\\n\\r\\nNot Implemented\\n\")\n' $FILE1 $port $port
fi
((++testCase))

out=$(diff <(timeout 5 curl -s -T $FILE1 localhost:$port/"$FILE2" -H "Content-Length: a") <(echo Bad Request))
printf "Test $testCase: "

if [ "$out" = "" ]; then
	printf "PASS\n"
else
	printf "FAIL. Difference found. Command run: diff <(curl -s -T $FILE1 localhost:$port/$FILE2 -H \"Content-Length: a\") <(echo Bad Request)\n"
fi
((++testCase))

out=$(diff <(timeout 5 curl -sT $FILE1 localhost:"$port"/"$FILE2" -H "Content-Length: 333a") <(echo Bad Request))
printf "Test $testCase: "

if [ "$out" = "" ]; then
	printf "PASS\n"
else
	printf "FAIL. Difference found. Command run: diff <(curl -s -T $FILE1 localhost:$port/$FILE2 -H \"Content-Length: 333a\") <(echo Bad Request)\n"
fi
((++testCase))

out=$(diff <(timeout 5 curl -sT $FILE1 localhost:"$port"/"$FILE2" -H "Content-Length: a333a") <(echo Bad Request))
printf "Test $testCase: "

if [ "$out" = "" ]; then
	printf "PASS\n"
else
	printf "FAIL. Difference found. Command run: diff <(curl -s -T $FILE1 localhost:$port/$FILE2 -H \"Content-Length: a333a\") <(echo Bad Request)\n"
fi
((++testCase))

FILE=this_file_is_more_than_19_characters.txt

if [ ! -f $FILE ]; then
	touch $FILE
fi

out=$(diff <(timeout 5 curl -s localhost:"$port"/"$FILE") <(echo Bad Request))
printf "Test $testCase: "

if [ "$out" = "" ]; then
	printf "PASS\n"
else
	printf "FAIL. Difference found. Command run: diff <(curl -s localhost:$port/$FILE) <(echo Bad Request)\n"
fi
((++testCase))

out=$(diff <(timeout 5 curl -sT $FILE1 localhost:"$port"/"$FILE") <(echo Bad Request))
printf "Test $testCase: "

if [ "$out" = "" ]; then
	printf "PASS\n"
else
	printf "FAIL. Difference found. Command run: diff <(curl -sT $FILE1 localhost:$port/$FILE) <(echo Bad Request)\n"
fi
((++testCase))

out=$(diff <(timeout 5 curl -sI localhost:"$port"/"$FILE") <(printf "HTTP/1.1 400 Bad Request\r\nContent-Length: 12\r\n\r\n"))
printf "Test $testCase: "
if [ "$out" = "" ]; then
	printf "PASS\n"
else
	printf 'FAIL. Difference found. Command run: diff <(curl -sI localhost:%s/%s) <(printf \"HTTP/1.1 400 Bad Request\\r\\nContent-Length: 12\\r\\n\\r\\n\")\n' $port $FILE
fi

((++testCase))
fn="this\$@^%()"
FILE="$fn".txt

out=$(diff <(timeout 5 curl -s localhost:"$port"/"$FILE") <(echo Bad Request))
printf "Test $testCase: "

if [ "$out" = "" ]; then
	printf "PASS\n"
else
	printf "FAIL. Difference found. Command run: diff <(curl -s localhost:$port/%s.txt) <(echo Bad Request)\n" $fn
fi
((++testCase))

printf "Test $testCase: "
out=$(diff <(timeout 5 curl -sT $FILE1 localhost:"$port"/"$FILE") <(echo Bad Request))

if [ "$out" = "" ]; then
	printf "PASS\n"
else
	printf "FAIL. Difference found. Command run: diff <(curl -sT $FILE1 localhost:$port/%s.txt) <(echo Bad Request)\n" $fn
fi
((++testCase))

out=$(diff <(timeout 5 curl -sI localhost:"$port"/"$FILE") <(printf "HTTP/1.1 400 Bad Request\r\nContent-Length: 12\r\n\r\n"))
printf "Test $testCase: "

if [ "$out" = "" ]; then
	printf "PASS\n"
else
	printf 'FAIL. Difference found. Command run: diff <(curl -sI localhost:%s/%s.txt) <(printf \"HTTP/1.1 400 Bad Request\\r\\nContent-Length: 12\\r\\n\\r\\n\")\n' $port $fn
fi
((++testCase))

#### Checking for a file that does not exist ####
#### Tests 51-52                             ####
echo ====Running File Not Found tests====
FILE=non_existent.txt

printf "Test $testCase: "
out=$(diff <(timeout 5 curl -s localhost:"$port"/"$FILE") <(echo File Not Found))

if [ "$out" = "" ]; then
	printf "PASS\n"
else
	printf "FAIL. diff <(curl -s localhost:$port/$FILE) <(echo File Not Found)\n"
fi
((++testCase))

printf "Test $testCase: "
out=$(diff <(timeout 5 curl -sI localhost:"$port"/"$FILE") <(printf "HTTP/1.1 404 File Not Found\r\nContent-Length: 15\r\n\r\n"))
if [ "$out" = "" ]; then
	printf "PASS\n"
else
	printf 'FAIL. Difference found. Command run: diff <(curl -sI localhost:%s/%s) <(printf \"HTTP/1.1 404 File Not Found\\r\\nContent-Length: 15\\r\\n\\r\\n\")\n' $port $FILE
fi
((++testCase))

#### Checking on a file that you do not have access to ####
#### Tests 53-55                                       ####
echo ====Running Forbidden tests====
FILE=forbidden.txt

if [ ! -f "forbidden.txt" ]; then
	touch forbidden.txt
	chmod -rw forbidden.txt
fi

printf "Test $testCase: "
out=$(diff <(timeout 5 curl -s localhost:"$port"/"$FILE") <(echo Forbidden))

if [ "$out" = "" ]; then
	printf "PASS\n"
else
	printf "FAIL. diff <(curl -s localhost:$port/$FILE) <(echo Forbidden)\n"
fi
((++testCase))

printf "Test $testCase: "
out=$(diff <(timeout 5 curl -sT r1.txt localhost:"$port"/"$FILE") <(echo Forbidden))

if [ "$out" = "" ]; then
	printf "PASS\n"
else
	printf "FAIL. diff <(curl -sT r1.txt localhost:$port/$FILE) <(echo Forbidden)\n"
fi
((++testCase))

printf "Test $testCase: "
out=$(diff <(timeout 5 curl -sI localhost:"$port"/"$FILE") <(printf "HTTP/1.1 403 Forbidden\r\nContent-Length: 10\r\n\r\n"))

if [ "$out" = "" ]; then
	printf "PASS\n"
else
	printf 'FAIL. Difference found. Command run: diff <(curl -sI localhost:%s/%s) <(printf \"HTTP/1.1 403 Forbidden\\r\\nContent-Length: 10\\r\\n\\r\\n\")\n' $port $FILE
fi
((++testCase))

#### Performing health check ####
#### Test 56                 ####
echo ====Healthcheck Test====
line_count=$(cat log_file | wc)
err_count=$(cat log_file | grep FAIL | wc)
log_tokens=( $line_count )
err_tokens=( $err_count )
num_lines=${log_tokens[0]}
num_errs=${err_tokens[0]}
out=$(diff <(timeout 5 curl -s localhost:$port/healthcheck) <(printf "$num_errs\n$num_lines\n"))

printf "Test $testCase: "
if [ "$out" = "" ]; then
	printf "PASS\n"
else
	printf "FAIL. Difference found. Command run: diff <(curl -s localhost$port/healthcheck) <(printf \"$num_errs"
	printf '\\'
	printf "$num_lines"
	printf 'n\\n")\n'
fi
((++testCase))

#### Checking to see whether or not your program will hang ####
#### Tests 57-63                                           ####
echo ====CL Arguments Test====
timeout 2 ./httpserver -N 5 -l log_file
if [ $? -eq 124 ]; then
	printf "Test $testCase: FAIL. Program timed out. Command run: timeout 2 ./httpserver -N 5 -l log_file\n"
else
	printf "Test $testCase: PASS\n"
fi
((++testCase))

timeout 2 ./httpserver -N 5 -l r1.txt
if [ $? -eq 124 ]; then
	printf "Test $testCase: FAIL. Program timed out. Command run: timeout 2 ./httpserver -N 5 -l r1.txt\n"
else
	printf "Test $testCase: PASS\n"
fi
((++testCase))

timeout 2 ./httpserver $(($port + 1)) -N -l log_file
if [ $? -eq 124 ]; then
	printf "$testCase: FAIL. Program timed out. Command run: timeout 2 ./httpserver $(($port + 1)) -N -l log_file\n"
else
	printf "Test $testCase: PASS\n"
fi
((++testCase))

timeout 2 ./httpserver $(($port + 2)) -l -N
if [ $? -ne 124 ]; then
	printf "$testCase: FAIL. Program timed out. Command run: timeout 2 ./httpserver $(($port + 2)) -l -N\n"
else
	printf "Test $testCase: PASS\n"
	rm -- \-N
fi
((++testCase))

timeout 2 ./httpserver $(($port + 3)) -l -l
if [ $? -ne 124 ]; then
	printf "$testCase: FAIL. Program timed out. Command run: timeout 2 ./httpserver $(($port + 3)) -l -l\n"
else
	printf "Test $testCase: PASS\n"
	rm -- \-l
fi
((++testCase))

timeout 2 ./httpserver $(($port + 4)) -l log_file
if [ $? -ne 124 ]; then
	printf "$testCase: FAIL. Program timed out. Command run: timeout 2 ./httpserver $(($port + 4)) -l -l\n"
else
	printf "Test $testCase: PASS\n"
fi
((++testCase))

timeout 2 ./httpserver $(($port + 5)) -N 8
if [ $? -ne 124 ]; then
	printf "$testCase: FAIL. Program timed out. Command run: timeout 2 ./httpserver $(($port + 5)) -l -l\n"
else
	printf "Test $testCase: PASS\n"
fi
((++testCase))

timeout 2 ./httpserver $(($port + 1)) -N abcd89dcba
if [ $? -eq 124 ]; then
	printf "$testCase: FAIL. Program timed out. Command run: timeout 2 ./httpserver $(($port + 6)) -l -l\n"
else
	printf "Test $testCase: PASS\n"
fi
((++testCase))

echo ====Concurrent Connections Stress Test====

OUTFILE=r8.txt

for i in {1..50}
do
	timeout 20 curl -sT r"$(((i % 7) + 1))".txt localhost:$port/$OUTFILE > /dev/null & timeout 20 wget -qO - localhost:$port/$OUTFILE > /dev/null &
done
wait

for i in {1..7}
do
	out=$(diff r"$i".txt $OUTFILE)

	if [ "$out" = "" ]; then
		break
	fi
done

printf "Test $testCase: "
if [ "$out" = "" ]; then
	printf "PASS\n"
else
	printf "FAIL. Difference found.\n"
fi
((++testCase))

printf "====All Done====\n"

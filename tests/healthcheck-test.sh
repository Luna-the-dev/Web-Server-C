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

printf "====Cleaning up files from previous runs if they exist, and clearing log_file====\n"
NUM_IN_FILES=7
iter=$(($NUM_IN_FILES+1))
while [ $iter -le $((3*$NUM_IN_FILES)) ]; do
	if [ -f r"$iter".txt ]; then
		rm -f r"$iter".txt
	fi
	((++iter))
done

((++testCase))

echo ====Healthcheck Test 1====
line_count=$(cat log_file | wc)
err_count=$(cat log_file | grep FAIL | wc)
log_tokens=( $line_count )
err_tokens=( $err_count )
num_lines=${log_tokens[0]}
num_errs=${err_tokens[0]}
out=$(diff <(curl -s localhost:$port/healthcheck) <(printf "$num_errs\n$num_lines\n"))
# diff <(curl -s localhost:8080/healthcheck) <(printf "7\n11\n")
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

#### Call GET on each test file and diff it with actual file ####
#### Tests 2-8                                               ####
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

echo ====Healthcheck Test 2====
line_count=$(cat log_file | wc)
err_count=$(cat log_file | grep FAIL | wc)
log_tokens=( $line_count )
err_tokens=( $err_count )
num_lines=${log_tokens[0]}
num_errs=${err_tokens[0]}
out=$(diff <(curl -s localhost:$port/healthcheck) <(printf "$num_errs\n$num_lines\n"))

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

#### Call HEAD on each test file and diff it with wc of file ####
#### Tests 10-16                                              ####
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

echo ====Healthcheck Test 3====
line_count=$(cat log_file | wc)
err_count=$(cat log_file | grep FAIL | wc)
log_tokens=( $line_count )
err_tokens=( $err_count )
num_lines=${log_tokens[0]}
num_errs=${err_tokens[0]}
out=$(diff <(curl -s localhost:$port/healthcheck) <(printf "$num_errs\n$num_lines\n"))

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

#### PUT file into another file, then diff the resulting files ####
#### Tests 18-24                                               ####
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

echo ====Healthcheck Test 4====
line_count=$(cat log_file | wc)
err_count=$(cat log_file | grep FAIL | wc)
log_tokens=( $line_count )
err_tokens=( $err_count )
num_lines=${log_tokens[0]}
num_errs=${err_tokens[0]}
out=$(diff <(curl -s localhost:$port/healthcheck) <(printf "$num_errs\n$num_lines\n"))

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

#### Call GET on the files created in the above loop and diff them with OG file ####
#### Test 26                                                                    ####
echo "====Running GETs on files created/updated with PUTs above===="

out=$(check_GET_Diff $INFILE 8 4)

printf "Test $testCase: "
if [ "$out" = "" ]; then
	printf "PASS\n"
else
	printf "FAIL. Difference found. Calling GET on [r8.txt, ..., r11.txt] and diffing them with $INFILE\n"
fi
((++testCase))

echo ====Healthcheck Test 5====
line_count=$(cat log_file | wc)
err_count=$(cat log_file | grep FAIL | wc)
log_tokens=( $line_count )
err_tokens=( $err_count )
num_lines=${log_tokens[0]}
num_errs=${err_tokens[0]}
out=$(diff <(curl -s localhost:$port/healthcheck) <(printf "$num_errs\n$num_lines\n"))

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

#### Call HEAD on each test file and diff it with wc and expected return of file ####
#### Tests 28                                                                    ####
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

echo ====Healthcheck Test 6====
line_count=$(cat log_file | wc)
err_count=$(cat log_file | grep FAIL | wc)
log_tokens=( $line_count )
err_tokens=( $err_count )
num_lines=${log_tokens[0]}
num_errs=${err_tokens[0]}
out=$(diff <(curl -s localhost:$port/healthcheck) <(printf "$num_errs\n$num_lines\n"))

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

#### Call put on same file twice then check output of GET ####
#### Tests 30-36                                          ####
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

echo ====Healthcheck Test 7====
line_count=$(cat log_file | wc)
err_count=$(cat log_file | grep FAIL | wc)
log_tokens=( $line_count )
err_tokens=( $err_count )
num_lines=${log_tokens[0]}
num_errs=${err_tokens[0]}
out=$(diff <(curl -s localhost:$port/healthcheck) <(printf "$num_errs\n$num_lines\n"))

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

#### Call put on same file twice then check output of HEAD ####
#### Tests 38-44                                           ####
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

echo ====Healthcheck Test 8====
line_count=$(cat log_file | wc)
err_count=$(cat log_file | grep FAIL | wc)
log_tokens=( $line_count )
err_tokens=( $err_count )
num_lines=${log_tokens[0]}
num_errs=${err_tokens[0]}
out=$(diff <(curl -s localhost:$port/healthcheck) <(printf "$num_errs\n$num_lines\n"))

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

#### Check for invalid resource names, content-lengths, hosts among other things ####
#### Tests 46-51                                                                 ####
echo "====Running Bad Request tests===="
FILE1=r1.txt
FILE2=r2.txt

printf "Test $testCase: "
out=$(diff <(curl -sI localhost:"$port"/"$FILE1" -H "Host: a a")  <(printf "HTTP/1.1 400 Bad Request\r\nContent-Length: 12\r\n\r\n"))

if [ "$out" = "" ]; then
	printf "PASS\n"
else
	printf 'FAIL. Difference found. Command run: diff <(curl -sI localhost:%s/%s -H \"Host: a a\") <(printf \"HTTP/1.1 400 Bad Request\\r\\nContent-Length: 12\\r\\n\\r\\n\")\n' $port $FILE
fi
((++testCase))

printf "Test $testCase: "
out=$(diff <(curl -s -T $FILE1 localhost:$port/"$FILE2" -H "Content-Length: a") <(echo Bad Request))

if [ "$out" = "" ]; then
	printf "PASS\n"
else
	printf "FAIL. Difference found. Command run: diff <(curl -s -T $FILE1 localhost:$port/$FILE2 -H \"Content-Length: a\") <(echo Bad Request)\n"
fi
((++testCase))

printf "Test $testCase: "
out=$(diff <(curl -s -T $FILE1 localhost:"$port"/"$FILE2" -H "Content-Length: 333a") <(echo Bad Request))

if [ "$out" = "" ]; then
	printf "PASS\n"
else
	printf "FAIL. Difference found. Command run: diff <(curl -s -T $FILE1 localhost:$port/$FILE2 -H \"Content-Length: 333a\") <(echo Bad Request)\n"
fi
((++testCase))

printf "Test $testCase: "
out=$(diff <(curl -s -T $FILE1 localhost:"$port"/"$FILE2" -H "Content-Length: a333a") <(echo Bad Request))

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

printf "Test $testCase: "
out=$(diff <(curl -sI localhost:"$port"/"$FILE") <(printf "HTTP/1.1 400 Bad Request\r\nContent-Length: 12\r\n\r\n"))
if [ "$out" = "" ]; then
	printf "PASS\n"
else
	printf 'FAIL. Difference found. Command run: diff <(curl -sI localhost:%s/%s) <(printf \"HTTP/1.1 400 Bad Request\\r\\nContent-Length: 12\\r\\n\\r\\n\")\n' $port $FILE
fi

((++testCase))
fn="this\$@^%()"
FILE="$fn".txt

printf "Test $testCase: "
out=$(diff <(curl -sI localhost:"$port"/"$FILE") <(printf "HTTP/1.1 400 Bad Request\r\nContent-Length: 12\r\n\r\n"))

if [ "$out" = "" ]; then
	printf "PASS\n"
else
	printf 'FAIL. Difference found. Command run: diff <(curl -sI localhost:%s/%s) <(printf \"HTTP/1.1 400 Bad Request\\r\\nContent-Length: 12\\r\\n\\r\\n\")\n' $port $FILE
fi
((++testCase))

echo ====Healthcheck Test 9====
line_count=$(cat log_file | wc)
err_count=$(cat log_file | grep FAIL | wc)
log_tokens=( $line_count )
err_tokens=( $err_count )
num_lines=${log_tokens[0]}
num_errs=${err_tokens[0]}
out=$(diff <(curl -s localhost:$port/healthcheck) <(printf "$num_errs\n$num_lines\n"))

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

#### Checking for a file that does not exist ####
#### Tests 53-54                             ####
echo ====Running File Not Found tests====
FILE=non_existent.txt

printf "Test $testCase: "
out=$(diff <(curl -s localhost:"$port"/"$FILE") <(echo File Not Found))

if [ "$out" = "" ]; then
	printf "PASS\n"
else
	printf "FAIL. diff <(curl -s localhost:$port/$FILE) <(echo File Not Found)\n"
fi
((++testCase))

printf "Test $testCase: "
out=$(diff <(curl -sI localhost:"$port"/"$FILE") <(printf "HTTP/1.1 404 File Not Found\r\nContent-Length: 15\r\n\r\n"))
if [ "$out" = "" ]; then
	printf "PASS\n"
else
	printf 'FAIL. Difference found. Command run: diff <(curl -sI localhost:%s/%s) <(printf \"HTTP/1.1 404 File Not Found\\r\\nContent-Length: 15\\r\\n\\r\\n\")\n' $port $FILE
fi
((++testCase))

echo ====Healthcheck Test 10====
line_count=$(cat log_file | wc)
err_count=$(cat log_file | grep FAIL | wc)
log_tokens=( $line_count )
err_tokens=( $err_count )
num_lines=${log_tokens[0]}
num_errs=${err_tokens[0]}
out=$(diff <(curl -s localhost:$port/healthcheck) <(printf "$num_errs\n$num_lines\n"))

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

echo ====Running HEAD on Healthcheck====
printf "Test $testCase: "
out=$(diff <(curl -sI localhost:"$port"/healthcheck) <(printf "HTTP/1.1 403 Forbidden\r\nContent-Length: 10\r\n\r\n"))
if [ "$out" = "" ]; then
	printf "PASS\n"
else
	printf 'FAIL. Difference found. Command run: diff <(curl -sI localhost:%s/healthcheck) <(printf "HTTP/1.1 403 Forbidden\r\nContent-Length: 10\r\n\r\n")\n' $port
fi
((++testCase))

printf "Test $testCase: "
out=$(diff <(curl -sI localhost:"$port"/healthcheck) <(printf "HTTP/1.1 403 Forbidden\r\nContent-Length: 10\r\n\r\n"))
if [ "$out" = "" ]; then
	printf "PASS\n"
else
	printf 'FAIL. Difference found. Command run: diff <(curl -sI localhost:%s/healthcheck) <(printf "HTTP/1.1 403 Forbidden\r\nContent-Length: 10\r\n\r\n")\n' $port
fi
((++testCase))

echo ====Healthcheck Test 11====
line_count=$(cat log_file | wc)
err_count=$(cat log_file | grep FAIL | wc)
log_tokens=( $line_count )
err_tokens=( $err_count )
num_lines=${log_tokens[0]}
num_errs=${err_tokens[0]}
out=$(diff <(curl -s localhost:$port/healthcheck) <(printf "$num_errs\n$num_lines\n"))

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

echo ====Running PUT on Healthcheck====
printf "Test $testCase: "
out=$(diff <(curl -sT r1.txt localhost:"$port"/healthcheck) <(printf "Forbidden\n"))
if [ "$out" = "" ]; then
	printf "PASS\n"
else
	printf 'FAIL. Difference found. Command run: diff <(curl -sT r1.txt localhost:%s/healthcheck) <(printf "Forbidden\n")\n' $port
fi
((++testCase))

printf "Test $testCase: "
out=$(diff <(curl -sT r2.txt localhost:"$port"/healthcheck) <(printf "Forbidden\n"))
if [ "$out" = "" ]; then
	printf "PASS\n"
else
	printf 'FAIL. Difference found. Command run: diff <(curl -sT r2.txt localhost:%s/healthcheck) <(printf "Forbidden\n")\n' $port
fi
((++testCase))

echo ====Healthcheck Test 12====
line_count=$(cat log_file | wc)
err_count=$(cat log_file | grep FAIL | wc)
log_tokens=( $line_count )
err_tokens=( $err_count )
num_lines=${log_tokens[0]}
num_errs=${err_tokens[0]}
out=$(diff <(curl -s localhost:$port/healthcheck) <(printf "$num_errs\n$num_lines\n"))

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
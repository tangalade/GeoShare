#!/bin/bash

# number of rounds that will be run for each test
NUM_ROUNDS=1000
# maximum number of parallel tests, uses xargs thread management
MAX_THREADS=100

# exponents of 2, tested in exact order
#  21-24 by 1
#   SIZES="$(seq 21 24)"
#  2-24 by 2
#   SIZES="$(seq 2 2 24)"
#  exact exponents
#   SIZES="8 10 3 21"
#SIZES="$(seq 10 2 16)"
SIZES="15"

# which request methods to test
REQUESTS="SET GET"

# which encoding methods to test
ENCODINGS="SSS"

# which buckets to test
#BUCKETS="aws-test-apne1"
BUCKETS="aws-test-apne1 aws-test-apse1 aws-test-apse2 aws-test-euw1 aws-test-sae1 aws-test-use1 aws-test-usw1 aws-test-usw2"

DATE=`date +%H`

LOG_PREFIX="logs/1k-lat-"
LOG_SUFFIX="-$DATE.csv"
DEBUG_LOG_PREFIX="logs/debug_"
DEBUG_LOG_SUFFIX="-$DATE.csv"

# example call
#./aws SET NONE rand-obj-0-1024-1 infile

# powers of 2
for EXP in ${SIZES}
do
    # calculate number of bytes in input object
    SIZE=$[2**$EXP]

    # remove temp log files
    rm -f $LOG_PREFIX$EXP$LOG_SUFFIX
    rm -f $DEBUG_LOG_PREFIX$EXP$DEBUG_LOG_SUFFIX

    echo "$SIZE: "
    for BUCKET in ${BUCKETS}
    do
	echo -en "\t$BUCKET("
	for REQ in ${REQUESTS}
	do
	    echo -n "$REQ("
	    for ENC in ${ENCODINGS}
	    do
		echo -n "$ENC.."
		THRESH=3
		if [ $ENC = "XOR" ]
		then
		    NUM=3
		else
		    NUM=5
		fi
		
# valid object names will only have lowercase letters, numbers, and hyphens
#  make name lowercase, and replace any '_' with '-'
		OBJ_NAME="rand-obj-$BUCKET-$SIZE"
		OBJ_NAME=`echo $OBJ_NAME | tr '[:upper:]' '[:lower:]'`
		OBJ_NAME=${OBJ_NAME//_/-}
		
		for (( ROUND=0; ROUND<$NUM_ROUNDS; ROUND++ ))
		do
                    echo $ROUND
		done | xargs -I ROUND -n 1 -P $MAX_THREADS sh -c "./test $REQ $ENC $THRESH $NUM $SIZE $BUCKET $OBJ_NAME-ROUND" 1>> "$LOG_PREFIX$EXP$LOG_SUFFIX" 2>> "$DEBUG_LOG_PREFIX$EXP"
	    done
	    echo -en "\b\b)..."
	done
	echo -e "\b\b\b)...[OK]"
    done
grep GET $LOG_PREFIX$EXP$LOG_SUFFIX > logs/1k-get-$DATE.csv
grep SET $LOG_PREFIX$EXP$LOG_SUFFIX > logs/1k-set-$DATE.csv
done

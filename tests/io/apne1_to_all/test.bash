#!/bin/bash

# number of rounds that will be run for each test
NUM_ROUNDS=10
# maximum number of parallel tests, uses xargs thread management
MAX_THREADS=8

# exponents of 2, tested in exact order
#  21-24 by 1
#   SIZES="$(seq 21 24)"
#  2-24 by 2
#   SIZES="$(seq 2 2 24)"
#  exact exponents
#   SIZES="8 10 3 21"
#SIZES="$(seq 10 2 16)"
SIZES="10"

# which request methods to test
REQUESTS="SET GET"

# whiche buckets to test
BUCKETS="aws-test-apne1 aws-test-apse1 aws-test-apse2 aws-test-euw1 aws-test-sae1 aws-test-use1 aws-test-usw1 aws-test-usw2"

LOG_PREFIX="logs/logfin_"
DEBUG_LOG_PREFIX="logs/debug_"

# example call
#./aws SET NONE rand-obj-0-1024-1 infile

# powers of 2
for EXP in ${SIZES}
do
    # calculate number of bytes in input object
    SIZE=$[2**$EXP]

    # remove temp log files
    rm -f $LOG_PREFIX$EXP
    rm -f $DEBUG_LOG_PREFIX$EXP

    echo -n "$SIZE: "
    for BUCKET in ${BUCKETS}
    do
	echo -n "$BUCKET("
	for REQ in ${REQUESTS}
	do
	    # valid object names will only have lowercase letters, numbers, and hyphens
	    # make name lowercase, and replace any '_' with '-'
	    OBJ_NAME="rand-obj-$BUCKET-$SIZE"
	    OBJ_NAME=`echo $OBJ_NAME | tr '[:upper:]' '[:lower:]'`
	    OBJ_NAME=${OBJ_NAME//_/-}

	    TIMEFORMAT="$REQ,$BUCKET,$SIZE,Client time,%E"

	    echo -n "$REQ.."
# this setup of loop works
#	    time for (( IDX=0; IDX<$NUM_ROUNDS; IDX++ ))
#            do
#		echo $IDX
#            done | xargs -I ROUND -n 1 -P $MAX_THREADS sh -c "./aws $REQ $METHOD $OBJ_NAME-ROUND $INFILE" 1>> "$LOG_PREFIX$EXP" 2>> "$DEBUG_LOG_PREFIX$EXP"
#	    exit
	    # GET calls have different parameters
	    if [ $REQ = "GET" ]
	    then
		for (( ROUND=0; ROUND<$NUM_ROUNDS; ROUND++ ))
                do
                    echo $ROUND
                done | xargs -I ROUND -n 1 -P $MAX_THREADS sh -c "./test $REQ $BUCKET $OBJ_NAME-ROUND" 1>> "$LOG_PREFIX$EXP" 2>> "$DEBUG_LOG_PREFIX$EXP"
	    else
		for (( ROUND=0; ROUND<$NUM_ROUNDS; ROUND++ ))
                do
                    echo $ROUND
                done | xargs -I ROUND -n 1 -P $MAX_THREADS sh -c "./test $REQ $SIZE $BUCKET $OBJ_NAME-ROUND" 1>> "$LOG_PREFIX$EXP" 2>> "$DEBUG_LOG_PREFIX$EXP"
	    fi
	done
	echo -en "\b\b)..."
    done
    echo "[OK]"
done

#!/bin/bash

# number of rounds that will be run for each test
NUM_ROUNDS=1
# maximum number of parallel tests, uses xargs thread management
#EDIT
MAX_THREADS=1

# exponents of 2, tested in exact order
#  21-24 by 1
#   SIZES="$(seq 21 24)"
#  2-24 by 2
#   SIZES="$(seq 2 2 24)"
#  exact exponents
#   SIZES="8 10 3 21"
#SIZES="$(seq 12 1 21)"
SIZES="$(seq 15 1 22)"

# which request methods to test
#EDIT
REQUESTS="SET GET"
#REQUESTS="SET"

# which encoding methods to test
ENCODINGS="AES-SSS"

# which buckets to test
BUCKETS="aws-test-apne1"
#BUCKETS="aws-test-apne1 aws-test-apse1 aws-test-apse2 aws-test-euw1 aws-test-sae1 aws-test-use1 aws-test-usw1 aws-test-usw2"

#which region is down
DOWNS="none"

DATE=`date +%H`

#EDIT
LOG_PREFIX="logs/lat-"
LOG_SUFFIX=".csv"
#LOG_SUFFIX="-$DATE.csv"
DEBUG_LOG_PREFIX="logs/debug_"
DEBUG_LOG_SUFFIX=""
#DEBUG_LOG_SUFFIX="-$DATE"

# example call
#./aws SET NONE rand-obj-0-1024-1 infile

# powers of 2
for EXP in ${SIZES}
do
    # calculate number of bytes in input object
    SIZE=$[2**$EXP]

    # remove temp log files
#    rm -f $LOG_PREFIX*
#    rm -f $DEBUG_LOG_PREFIX*
    rm -f $LOG_PREFIX$EXP-*
    rm -f $DEBUG_LOG_PREFIX$EXP-*

    echo -n "$SIZE: "
    for BUCKET in ${BUCKETS}
    do
#	echo -n "$BUCKET("
	for REQ in ${REQUESTS}
	do
	    echo -n "$REQ("
	    for ENC in ${ENCODINGS}
	    do
		THRESH=3
		if [ $ENC = "XOR" ]
		then
		    NUMS="3"
		elif [ $ENC = "SSS" ]
		then
		    NUMS="3 4 5"
		elif [ $ENC = "AES-SSS" ]
		then
		    NUMS="3 5"
		else
		    echo "ERROR: invalid encoding, $ENC"
		    exit
		fi
		
# valid object names will only have lowercase letters, numbers, and hyphens
#  make name lowercase, and replace any '_' with '-'
		for NUM in ${NUMS}
		do
		    echo -n "$ENC($THRESH,$NUM).."
#		    for (( ROUND=0; ROUND<$NUM_ROUNDS; ROUND++ ))
#		    do
#			echo $ROUND
#		    IFS=$("\n")
		    for DOWN in ${DOWNS}
		    do
			OBJ_NAME="rand-folder/rand-obj-$BUCKET-$SIZE-$ENC-$DOWN"
			OBJ_NAME=`echo $OBJ_NAME | tr '[:upper:]' '[:lower:]'`
			OBJ_NAME=${OBJ_NAME//_/-}

			ROUND=0
			cat placement-apne1.csv | while read -a line
			do
			    # puts the round number in OBJ_NAME and the object placements as the frag destinations
			    echo $ROUND $line
			    ROUND=$((ROUND+1));
			done | xargs -I ROUND -n 1 -P $MAX_THREADS sh -c "./test $REQ $ENC $THRESH $NUM $SIZE $BUCKET $OBJ_NAME-ROUND $DOWN" 1>> "$LOG_PREFIX$DOWN$LOG_SUFFIX" 2>> "$DEBUG_LOG_PREFIX$DOWN$DEBUG_LOG_SUFFIX"
		    done
		done
	    done
	    echo -en "\b\b)..."
	done
	echo "[OK]"
    done
done

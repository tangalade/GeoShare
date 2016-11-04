#!/bin/bash

# number of rounds that will be run for each test
NUM_ROUNDS=1000

# object sizes
SIZES="40"

# request methods
REQUESTS="SET GET"

# encoding methods to test
ENCODINGS="AES-SSS(3,5,1) AES-SSS(3,5,2) AES-SSS(3,5,3) AES-SSS(3,5,4) AES-SSS(3,5,5) AES-SSS(3,5,6) AES-SSS(3,5,7) AES-SSS(3,5,8) "

# destination regions
REGIONS="use1 usw1 usw2 euw1 sae1 apne1 apse1 apse2"

#which region is down
DOWNS="none"

LOG_PREFIX="logs/lat"
LOG_SUFFIX=".csv"
DEBUG_LOG_PREFIX="logs/debug"
DEBUG_LOG_SUFFIX=""

for SIZE in ${SIZES}
do
    # remove old log files
    rm -f $LOG_PREFIX*
    rm -f $DEBUG_LOG_PREFIX*

    echo "$SIZE:"
    for REGION in ${REGIONS}
    do
	echo -n "  $REGION("
	for REQUEST in ${REQUESTS}
	do
	    echo -n "$REQUEST("
	    for ENCODING in ${ENCODINGS}
	    do
		echo -n "$ENCODING.."

# valid object names will only have lowercase letters, numbers, and hyphens
#  make name lowercase, and replace any '_' with '-'
		OBJ_NAME="rand-folder/rand-obj-$SIZE-$ENCODING"
		OBJ_NAME=`echo $OBJ_NAME | tr '[:upper:]' '[:lower:]'`
		OBJ_NAME=${OBJ_NAME//_/-}

		./test -r "$REQUEST"                                \
		    -e "$ENCODING"                                  \
		    -s $SIZE                                        \
		    -o "$OBJ_NAME"                                  \
		    -d "$REGION"                                    \
		    --network-loops=$NUM_ROUNDS                     \
		    -p                                              \
		    1>> "$LOG_PREFIX-$REGION$LOG_SUFFIX"            \
		    2>> "$DEBUG_LOG_PREFIX-$REGION$DEBUG_LOG_SUFFIX"
	    done
	    echo -en "\b\b)..."
	done
	echo -e "\b\b\b)....[OK]"
    done
done

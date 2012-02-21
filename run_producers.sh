#!/bin/bash
#
# run_producers.sh <command> <number of times>
#

cmd=$1
num=$2

if [ ! $num ] 
then
  num=1
fi

if [ "x$cmd" == "x" ]
then
  cmd='./mqproducer'
fi

count=0

while [ "$count" -lt "$num" ]
do
  run_cmd="$cmd -t qwer/$count"
  echo $run_cmd 
  eval $run_cmd&
  count=`expr $count + 1`
done 

echo "running!"


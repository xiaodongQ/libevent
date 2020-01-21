#!/bin/bash

# 跑多次客户端算一个平均时间(gettimeofday中的时间)
# clock() cost:540.000000 ms, gettimeofday():623 ms
totalgetms=0
totalclockms=0
times=5
for ((i=0; i<$times; i++))
do
    # clock() cost:540.000000 ms, gettimeofday():623 ms
    timeline=$(./multi_client|grep "gettimeofday():")
    
    # clock()
    ms1=$(echo $timeline|awk -F 'gettimeofday' '{print $1}'|awk -F':' '{print $2}'|awk -F' ' '{print $1}')
    # let "totalclockms = totalclockms + ms1"
    # 或((totalms=totalms+ms))
    # 由于是小数，let会报错，用bc计算
    totalclockms=$(echo "scale=2;$totalclockms+$ms1"|bc)

    # gettimeofday()
    ms2=$(echo $timeline|awk -F 'gettimeofday' '{print $2}'|awk -F':' '{print $2}'|awk -F' ' '{print $1}')
    let "totalgetms += ms2"

    echo "i:$i, clock[$ms1 ms, total:$totalclockms], gettimeofday[$ms2 ms, total:$totalgetms]"
done

echo "avg clock() cost: $(echo "$totalclockms/$times" | bc) ms, gettimeofday():$(echo "$totalgetms/$times"|bc) ms"
REGIONS=(use1 usw2 usw1 euw1 apse1 apne1 apse2 sae1)
IPS=(54.152.103.195 54.149.64.21 54.153.24.36 54.154.165.0 54.169.222.151 54.65.16.4 54.66.179.175 54.207.31.59)

for REG in ${REGIONS[*]}
do
    for line in `cat lat-get-$REG.csv`
    do
        front=$(echo $line | cut -f1 -d.)
        end=$(echo $line | cut -f2 -d.)
        dist=$((6-${#end}))
        total=$front
        total=${total}.
        while [ $dist -gt 0 ]
        do
            total=${total}0
            dist=$(($dist - 1))
        done
        total=${total}$end
        echo $total >> lat-get-$REG-mod.csv
    done
done

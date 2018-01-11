#!/usr/bin/env bash

zServAddr=$1
zServPort=$2
zSelfAddr=$3

zCpuTotalPrev=0
zCpuTotalCur=0

zCpuSpentPrev=0
zCpuSpentCur=0

zDiskIOPrev=0
zDiskIOCur=0

zNetIOPrev=0
zNetIOCur=0

zCpuTotal=0
zCpuSpent=0
zMemTotal=0
zMemSpent=0
zDiskIOSpent=0
zNetIOSpent=0
zDiskUsage=0
zLoadAvg5=0

zReadData() {
    # CPU: total = user + system ＋nice + idle, spent = total - idle
    # /proc/stats
    # user   (1) Time spent in user mode.
    # nice   (2) Time spent in user mode with low priority (nice).
    # system (3) Time spent in system mode.
    # idle   (4) Time spent in the idle task.  This value should be USER_HZ times the second entry in the /proc/uptime pseudo-file.
    zCpuTotalCur=`cat /proc/stat | head -1 | awk -F' ' '{print $2,$3,$4,$5}' | tr ' ' '+' | bc`
    zCpuTotal=$((${zCpuTotalCur} - ${zCpuTotalPrev}))

    # when unsigned int overflow...
    if [[ 0 -gt ${zCpuTotal} ]]; then
        return -1
    fi

    zCpuSpentCur=`cat /proc/stat | head -1 | awk -F' ' '{print $2,$3,$4}' | tr ' ' '+' | bc`
    zCpuSpent=$((${zCpuSpentCur} - ${zCpuSpentPrev}))

    # when unsigned int overflow...
    if [[ 0 -gt ${zCpuSpent} ]]; then
        return -1
    fi

    # MEM: total = MemTotal, spent = MemTotal - MemFree - Buffers - Cached
    # /proc/meminfo
    # MemTotal %lu
    #        Total usable RAM (i.e., physical RAM minus a few reserved bits and the kernel binary code).
    # MemFree %lu
    #        The sum of LowFree+HighFree.
    # Buffers %lu
    #        Relatively temporary storage for raw disk blocks that shouldn't get tremendously large (20MB or so).
    # Cached %lu
    #        In-memory cache for files read from the disk (the page cache).  Doesn't include SwapCached.
    zMemTotal=`cat /proc/meminfo | fgrep 'MemTotal' | grep -o '[0-9]\+'`  # Warning: memory hot-plug
    zMemSpent=$((${zMemTotal} -\
        `cat /proc/meminfo | grep -E '^(MemFree|Buffers|Cached)' | grep -o '[0-9]\+' | tr '\n' '+' | grep -oP '.*(?=\+$)' | bc`))

    # DISK tps: /proc/diskstats
    #
    # ==== for disk ====
    # spent = <Field 1> - <Field 1 prev> + <Field 5> - <Field 5 prev>
    #
    # Field  1 -- # of reads completed
    #     This is the total number of reads completed successfully.
    # Field  2 -- # of reads merged, field 6 -- # of writes merged
    #     Reads and writes which are adjacent to each other may be merged for
    #     efficiency.  Thus two 4K reads may become one 8K read before it is
    #     ultimately handed to the disk, and so it will be counted (and queued)
    #     as only one I/O.  This field lets you know how often this was done.
    # Field  3 -- # of sectors read
    #     This is the total number of sectors read successfully.
    # Field  4 -- # of milliseconds spent reading
    #     This is the total number of milliseconds spent by all reads (as
    #     measured from __make_request() to end_that_request_last()).
    # Field  5 -- # of writes completed
    #     This is the total number of writes completed successfully.
    # Field  6 -- # of writes merged
    #     See the description of field 2.
    # Field  7 -- # of sectors written
    #     This is the total number of sectors written successfully.
    # Field  8 -- # of milliseconds spent writing
    #     This is the total number of milliseconds spent by all writes (as
    #     measured from __make_request() to end_that_request_last()).
    # Field  9 -- # of I/Os currently in progress
    #     The only field that should go to zero. Incremented as requests are
    #     given to appropriate struct request_queue and decremented as they finish.
    # Field 10 -- # of milliseconds spent doing I/Os
    #     This field increases so long as field 9 is nonzero.
    # Field 11 -- weighted # of milliseconds spent doing I/Os
    #     This field is incremented at each I/O start, I/O completion, I/O
    #     merge, or read of these stats by the number of I/Os in progress
    #     (field 9) times the number of milliseconds spent doing I/O since the
    #     last update of this field.  This can provide an easy measure of both
    #     I/O completion time and the backlog that may be accumulating.
    #
    # ==== for disk partition ====
    # spent = <Field 1> - <Field 1 prev> + <Field 3> - <Field 3 prev>
    #
    # Field  1 -- # of reads issued
    #     This is the total number of reads issued to this partition.
    # Field  2 -- # of sectors read
    #     This is the total number of sectors requested to be read from this
    #     partition.
    # Field  3 -- # of writes issued
    #     This is the total number of writes issued to this partition.
    # Field  4 -- # of sectors written
    #     This is the total number of sectors requested to be written to
    #     this partition.
    zDiskIOCur=0;
    for x in `cat /proc/diskstats | fgrep -v loop | grep -v '[a-Z][0-9]' | awk -F' ' '{print $4,$8}'`
    do
        let zDiskIOCur+=$x
    done
    zDiskIOSpent=$((${zDiskIOCur} - ${zDiskIOPrev}))

    # when unsigned int overflow...
    if [[ 0 -gt ${zDiskIOSpent} ]]; then
        return -1
    fi

    # NET tps: /proc/net/dev
    zNetIOCur=0;
    for x in `cat /proc/net/dev | fgrep -v '|' | grep -vP '^\s*lo:' | awk -F' ' '{print $3,$11}'`
    do
        let zNetIOCur+=$x
    done
    zNetIOSpent=$((${zNetIOCur} - ${zNetIOPrev}));

    # when unsigned int overflow...
    if [[ 0 -gt ${zNetIOSpent} ]]; then
        return -1
    fi

    # DISK USAGE MAX: df
    zDiskUsage=0
    for x in `df | grep '^/dev' | awk -F' ' '{print $5}' | uniq | grep -o '[^%]\+'`
    do
        if [[ $i -lt $x ]]
        then
            zDiskUsage=$x
        fi
    done


    # LOADAVG5 /proc/loadavg
    zCPUNum=`cat /proc/cpuinfo | grep -c processor`  # Warning: CPU hot-plug
    zLoadAvg5=`echo "\`cat /proc/loadavg | awk -F' ' '{print $3}'\` * 10000 / ${zCPUNum}" | bc | grep -o '^[0-9]\+'`
}

# pre-exec once
zReadData

zCpuTotalPrev=${zCpuTotalCur}
zCpuSpentPrev=${zCpuSpentCur}
zDiskIOPrev=${zDiskIOCur}
zNetIOtPrev=${zNetIOCur}

# generate one udp socket
exec 7>/dev/udp/${zServAddr}/${zServPort}

# clean old process...
if [[ 0 -lt `ps ax | grep $0 | grep \`cat /tmp/.supervisor.pid\` | wc -l` ]]
then
    kill `cat /tmp/.supervisor.pid`
fi

echo $$ >/tmp/.supervisor.pid

# start...
while :
do
    sleep 10

    zReadData

    if [[ 0 -gt $? ]]
    then
        continue
    fi

    # '8' is a command !!!
    echo "8INSERT INTO supervisor_log VALUES (${zSelfAddr},`date +%s`,${zCpuTotal},${zCpuSpent},${zMemTotal},${zMemSpent},${zDiskIOSpent},${zNetIOSpent},${zDiskUsage},${zLoadAvg5})">&7

    zCpuTotalPrev=${zCpuTotalCur}
    zCpuSpentPrev=${zCpuSpentCur}
    zDiskIOPrev=${zDiskIOCur}
    zNetIOtPrev=${zNetIOCur}
done



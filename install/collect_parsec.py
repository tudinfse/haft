#!/usr/bin/env python

fres = open('/data/parsec_raw.txt', 'w')
fres.write('input bench threads instructions start commit abort capacity conflict time\n')

def collect(filename, suffix):
    with open(filename, 'r') as f:
        prog_type   = 'DUMMY'
        benchmark   = 'DUMMY'
        num_threads = 0

        instructions= 0
        start       = 0
        commit      = 0
        abort       = 0
        capacity    = 0
        conflict    = 0

        time        = 0.0

        isPerf = False

        lines = f.readlines()
        for l in lines:
            if l.startswith('--- Running '):
                benchmark   = l.split('--- Running ')[1].split(' ')[0]
                num_threads = int(l.split('--- Running ')[1].split(' ')[1])
                prog_type   = l.split('--- Running ')[1].split(' ')[2]
                continue

            if "Performance counter stats for" in l:
                isPerf = True
                continue

            if isPerf and "instructions" in l:
                instructions = int(l.split()[0])
                continue

            if isPerf and "cpu/tx-start/" in l:
                start = int(l.split()[0])
                continue

            if isPerf and "cpu/tx-commit/" in l:
                commit = int(l.split()[0])
                continue

            if isPerf and "cpu/tx-abort/" in l:
                abort = int(l.split()[0])
                continue

            if isPerf and "cpu/tx-capacity/" in l:
                capacity = int(l.split()[0])
                continue

            if isPerf and "cpu/tx-conflict/" in l:
                conflict = int(l.split()[0])
                continue

            if isPerf and "seconds time elapsed" in l:
                time = float(l.split()[0])
                isPerf = False
                
                benchmark += suffix

                fres.write('%s %s %d %d %d %d %d %d %d %f\n' % 
                    (prog_type, benchmark, num_threads,
                     instructions, start, commit, abort, capacity, conflict, time))
                continue

collect('/data/parsec.log', '')

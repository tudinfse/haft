from __future__ import print_function
import os
import os.path
from collections import namedtuple

# ---------------------------- CONSTANTS ------------------------------------- #
RESULTS   = "result.txt"
AGGRLOG   = "result.log"
LOGDIR    = "logs"
FULLLOG   = ".log"

# --------------------- COLLECT ALL RESULTS FROM ALL LOGS -------------------- #
def collectLogs():
    # aggregated log
    aggrlog = ""

    # dict { Entry -> number }
    Entry = namedtuple("Entry", ["benchmark", "version", "hardening", "outcome"])
    resdict = {}

    for dirpath, dirnames, filenames in os.walk(LOGDIR):
        for filename in [f for f in filenames if f.endswith(FULLLOG)]:
            logfilename = os.path.join(dirpath, filename)
            aggrlog += "----- %s -----\n" % logfilename

            logfilebody = False
            for line in open(logfilename):
                if line.startswith('----- log -----'):
                    logfilebody = True
                    continue

                if logfilebody == False:  continue

                # add log entry to aggregated log
                aggrlog += line

                # parse each log entry of form "000000   MASKED"
                entry = Entry(benchmark = filename.split('.')[0],
                              version   = dirpath.split(os.sep)[1],
                              hardening = dirpath.split(os.sep)[2],
                              outcome   = line.split()[1])
                resdict[entry] = resdict.get(entry, 0) + 1

    with open(AGGRLOG, "w") as f:
        f.write(aggrlog)
        f.close()

    with open(RESULTS, "w") as f:
        f.write("benchmark version hardening outcome number\n")
        entries = resdict.keys()
        entries.sort()
        for entry in entries:            
            f.write("%s %s %s %s %d\n" % (entry.benchmark,
                                          entry.version,
                                          entry.hardening,
                                          entry.outcome,
                                          resdict[entry]))
        f.close()


def main():
    collectLogs()

if __name__ == "__main__":
    main()

from __future__ import print_function
import argparse
import random
import subprocess
import os
import signal
import time
import hashlib
import shutil

# ---------------------------- LOCAL PATHS ----------------------------------- #
GDB = "~/bin/binutils-gdb/gdb/gdb"
SDE = "~/bin/intel_sde/sde64"

# ---------------------------- CONSTANTS ------------------------------------- #
DUMPINFO = True
MAXTRIES = 10

# inject only in TSX-covered parts (otherwise inject in all code)
ONLYTSX     = False

SORTOUTPUT  = False
ERROROUTPUT = False

LIMIT   = 1   # number of fault injection runs
TIMEOUT = 1   # in seconds

DYNTRACE_REGSEP = "|"

LOGDIR    = "logs"
FULLLOG   = "log.log"
GDBSCRIPT = "gdbscript"
TSXLOG    = "tsxlog"
SDELOG    = "sdelog"
GDBLOG    = "gdblog"

DEBUGPORT = 10000

# not all GP registers are supported:
#   - we do not inject into rflags, rsp and rip, these are considered control-flow
SUPPORTED_GP_REGS = ["rax", "rbx", "rcx", "rdx", "rsi", "rdi", "rbp", 
                  "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15"]

# instructions that must be definitely ignored (control-flow instructions)
IGNORED_INSTS = ["pop", "push", "ret", "call", "cmp", "enter", "leave"]

# name for Intel RTM in dynamic trace produced by Intel SDE
RTM_TYPE_NAME = "RTM"
# name for common x86 instrs in dynamic trace produced by Intel SDE
BASE_TYPE_NAME = "BASE"

XBEGIN_NAME = "xbegin"
XEND_NAME   = "xtest"   # xend is always preceeded by xtest, so we choose xtest 
                        # also xtest executes always, unlike xend


# ---------------------------- GLOBALS --------------------------------------- #

# instr address -> [total number of invocations, output reg, next instr address]
insts = {}
# total number of invocations across all insts
insts_totalinvocs = 0

# reference output for this program
ref_output = ""
# file with binary output written by the program; compare using md5
binary_output = ""

# ------------------------------- HELPERS ------------------------------------ #
def run2(args1, args2, timeout):
    class Alarm(Exception):
        pass

    def alarm_handler(signum, frame):
        raise Alarm

    if DUMPINFO:
        print(args1)
        print(args2)

    p1 = subprocess.Popen(args1, shell = True
            , stdout = subprocess.PIPE
            , stderr = subprocess.PIPE
            , preexec_fn = os.setsid)

    # wait a bit until first process is loaded
    time.sleep(2)

    p2 = subprocess.Popen(args2, shell = True
            , stdout = subprocess.PIPE
            , stderr = subprocess.PIPE
            , preexec_fn = os.setsid)

    signal.signal(signal.SIGALRM, alarm_handler)
    signal.alarm(timeout)

    try:
        stdout2, stderr2 = p2.communicate()
        stdout1, stderr1 = p1.communicate()
        if timeout != -1:
            signal.alarm(0)
    except Alarm:
        try: 
            os.killpg(p2.pid, signal.SIGKILL)
            os.killpg(p1.pid, signal.SIGKILL)
        except OSError:
            pass
        return -1, '', 'TIMEOUT', -1, '', 'TIMEOUT'
    return p1.returncode, stdout1, stderr1, p2.returncode, stdout2, stderr2


def initLog(refoutput, dyntracefile, rtmmode, program, args):
    fulllogfile = "%s/%s" % (LOGDIR, FULLLOG)
    with open(fulllogfile, "w") as f:
        f.write("----- info -----\n")
        f.write("    program: %s\n" % program)
        f.write("       args: %s\n" % args)
        f.write("\n")
        f.write(" ref output: %s\n" % refoutput)
        f.write("  dyn trace: %s\n" % dyntracefile)
        f.write("   rtm mode: %s\n" % rtmmode)
        f.write("\n")
        f.write("----- log -----\n")
        f.close()
    

# ------------------- IDENTIFY INSTRUCTIONS TO INJECT INTO ------------------- #
def identifyInstsInOneThread(dyntracefile, examinethreadid):
    global insts_totalinvocs

    insts_modified = False
    in_rtm = False
    last_inst_addr = "DUMMY"

    with open(dyntracefile, "r") as f:
        for line in f:
            if line.strip() == "":
                continue

            inst_splitted = line.split()
            
            if len(inst_splitted) < 5 or inst_splitted[1] != "INS":
                continue

            # dissect parts of line
            thread_id  = inst_splitted[0].replace("TID", "").replace(":", "")
            inst_addr  = inst_splitted[2]
            inst_type  = inst_splitted[3]   # category, e.g, "BASE" and "RTM"
            inst_name  = inst_splitted[4]   # mnemonic, e.g. "xor"

            if int(thread_id) != examinethreadid:
                # we ignore what other threads do
                continue

            # update last added to insts instruction with its successor
            if last_inst_addr != "DUMMY":
                insts[last_inst_addr][2] = inst_addr
                last_inst_addr = "DUMMY"

            if inst_type == RTM_TYPE_NAME:
                if inst_name == XBEGIN_NAME: in_rtm = True
                if inst_name == XEND_NAME:   in_rtm = False
                continue

            if ONLYTSX == True:
                if in_rtm == False:
                    # instruction is not in RTM-covered portion of code, ignore
                    continue

            if inst_name in IGNORED_INSTS:
                continue
            
            if DYNTRACE_REGSEP in line:
                # --- get GP register
                (_, regs_str) = line.split(DYNTRACE_REGSEP)
                # get output register name
                regs_str = regs_str.split(",")[0]   # leave only first reg
                reg_name = regs_str.split("=")[0].strip()
                # not all regs are supported
                if reg_name not in SUPPORTED_GP_REGS:
                    continue
            elif "SSE" in inst_type:
                # --- get SSE (xmm) register
                if len(inst_splitted) < 6:
                    continue
                # get output register name
                reg_name = inst_splitted[5].split(",")[0]
                # only xmm regs are supported
                if reg_name.startswith("xmmword") == True:
                    continue
                if reg_name.startswith("xmm") == False:
                    continue
            else:
                # --- all other instructions are ignored
                continue

            if inst_addr not in insts:
                # initialize new instruction
                insts[inst_addr] = [1, reg_name, "DUMMY"]
            else:
                # increment number of invocations for existing instruction
                insts[inst_addr][0] += 1

            insts_totalinvocs += 1
            insts_modified = True
            last_inst_addr = inst_addr

        f.close()
    return insts_modified


def identifyInsts(dyntracefile):
    # run identifyInstsInOneThread on all threads in program
    # except TID0 (thread 0 is main thread which does not do real processing)
    examinethreadid = 1
    while True:
        insts_modified = identifyInstsInOneThread(dyntracefile, examinethreadid)
        if insts_modified == False:
            break
        examinethreadid += 1

    assert(len(insts) > 1)
    if DUMPINFO:
        print("[examined %d threads]" % (examinethreadid-1))
#        print("insts = %s" % insts)


# ---------------------- WRITE GDB SCRIPT FOR INJECTION ---------------------- #
def writeScript(scriptfile, instaddr, numinvoc, regname, mask):
    if instaddr == "DUMMY":
        return

    with open(scriptfile, "w") as f:
        f.write("target remote :%d\n" % DEBUGPORT)
        f.write("tb *%s\n" % instaddr)          # set breakpoint on instr address
        f.write("ignore 1 %d\n" % (numinvoc-1)) # ignore this breakpoint x times
        f.write("commands 1\n")

        # inject fault in output register
        if regname.startswith("xmm"):
            f.write("  p $%s.uint128\n" % regname)
            f.write("  set $%s.v2_int64[0] = $%s.v2_int64[0] ^ %d\n" % (regname, regname, mask))
            f.write("  p $%s.uint128\n" % regname)
        elif regname.startswith("rflags"):
            f.write("  p $eflags\n")
            f.write("  set $eflags = $eflags ^ 0xC5\n") # flip CF, PF, ZF, and SF
            f.write("  p $eflags\n")
        else:
            f.write("  p $%s\n" % regname)
            f.write("  set $%s = (long long) $%s ^ %d\n" % (regname, regname, mask))
            f.write("  p $%s\n" % regname)

        f.write("  continue\n")
        f.write("end\n")
        f.write("continue\n")
        f.write("p \"Going to detach...\"\n")
        f.write("detach\n")

        f.close()


# --------------------------- INJECT RANDOM FAULT ---------------------------- #
def injectFault(rtmmode, index, program, args):
    for trynum in range(0, MAXTRIES):
        numinvoc       = -1
        regname        = 'DUMMY'
        injectinstaddr = 'DUMMY'

        # weighted random inst address based on {keys, totalinvoc} pairs 
        rnd = random.randint(1, insts_totalinvocs)
        for instaddr in insts.keys():
            if rnd <= insts[instaddr][0]:
                numinvoc       = rnd
                regname        = insts[instaddr][1]
                injectinstaddr = insts[instaddr][2]
                break
            rnd -= insts[instaddr][0]

        assert(numinvoc >= 0)
        # corrupt low 8 bits
        mask       = random.randint(1, 255)
        # restrict only to first 300 invocations, otherwise too slow
        numinvoc   = numinvoc % 300

        scriptfile = "%s/%s_%06d.%s" % (LOGDIR, program, index, GDBSCRIPT)
        writeScript(scriptfile, injectinstaddr, numinvoc, regname, mask)

        tsxlogfile = "%s/%s_%06d.%s" % (LOGDIR, program, index, TSXLOG)
#        sde_run = "%s -tsx_log_flush 1 -tsx_log_file 1 -tsx_file_name %s -tsx_debug_log 2 -rtm-mode %s -debug -debug-port %d -- %s %s" % \
        sde_run = "%s -tsx_log_flush 0 -tsx_log_file 0 -tsx_file_name %s -tsx_debug_log 0 -rtm-mode %s -debug -debug-port %d -- %s %s" % \
                    (SDE, tsxlogfile, rtmmode, DEBUGPORT, program, args)

        gdb_run = "%s --batch --command=%s --args %s %s" % \
                    (GDB, scriptfile, program, args)

        retcode1, stdout1, stderr1, retcode2, stdout2, stderr2 = run2(sde_run, gdb_run, TIMEOUT)

        sde_log = "[return code: %d]\n\n---------- stderr ----------\n%s\n\n---------- stdout ----------\n%s" % \
                    (retcode1, stderr1, stdout1)
        gdb_log = "[return code: %d]\n\n---------- stderr ----------\n%s\n\n---------- stdout ----------\n%s" % \
                    (retcode2, stderr2, stdout2)

        res = "DUMMY"
        if retcode1 == -1 or retcode2 == -1:
            # timeout signaled
            res = "HANG"            
        elif retcode2 != 0:
            # gdb failed -- this is weird
            res = "GDB"
        elif retcode1 != 0:
            # program failed
            if retcode1 == 2:   res = "ILR"
            elif (retcode1 == 255 and
                  stdout1.startswith("E: Unable to create debugger connection")):
                res = "GDB"
            elif (retcode1 == 132 and "Illegal instruction" in stderr1):
                # FIXME: some bug in SDE that sporadically leads to random "illegal instruction"
                res = "SDE"
            elif retcode1 == 1:
                if "SDE PINTOOL EXITNOW ERROR" in stderr1:    res = "SDE"
                elif "unaligned memory reference" in stderr1: res = "OS"
                else:                                         res = "PROG"
            else:               res = "OS"
        else:
            # both program and gdb exited nicely -- maybe it's a SDC?
            if binary_output != "":
                # binary, calc md5 of binary_output and compare with ref
                prog_output = hashlib.md5(open(binary_output, 'rb').read()).hexdigest()
            else:
                # normal text, remove header, sort if needed and compare with ref
                if ERROROUTPUT:
                    tmplist = stderr1.splitlines(True)
                    if len(tmplist) > 0 and "TSX log collection started" in tmplist[0]:
                        # remove line of TSX info
                        tmplist = tmplist[1:] 
                else:
                    tmplist = stdout1.splitlines(True)
                    tmplist = tmplist[3:] # remove 3 lines of SDE info
                if SORTOUTPUT:
                    tmplist.sort()
                prog_output = ''.join(tmplist)

            if prog_output == ref_output: res = "MASKED"
            else:                         res = "SDC"

        if res == "SDE" or res == "GDB":
            # we want to silently ignore SDE & GDB failures and retry FI again
            continue

        # ----- log everything
        sdelogfile = "%s/%s_%06d.%s" % (LOGDIR, program, index, SDELOG)
        gdblogfile = "%s/%s_%06d.%s" % (LOGDIR, program, index, GDBLOG)
        fulllogfile = "%s/%s" % (LOGDIR, FULLLOG)
        with open(sdelogfile, "w") as f:
            f.write(sde_log)
            f.close()
        with open(gdblogfile, "w") as f:
            f.write(gdb_log)
            f.close()
        with open(fulllogfile, "a") as f:
            f.write("%06d   %6s\n" % (index, res))
            f.close()
        # it was a succesfull fault injection, stop trying
        return
    

# ------------------------------- MAIN FUNCTION ------------------------------ #
rtmmodes = ['full', 'nop']

parser = argparse.ArgumentParser(description='GDB Fault Injector')
parser.add_argument('-p', '--program',
                    required=True,
                    help='Program under test')
parser.add_argument('-a', '--arguments',
                    default="",
                    help='Program under test')
parser.add_argument('-d', '--dyntrace',
                    required=True,
                    help='Dynamic trace log obtained via Intel SDE')
parser.add_argument('-m', '--rtmmode',
                    required=True,
                    choices=rtmmodes,
                    help='RTM mode to pass to Intel SDE')
parser.add_argument('-r', '--refoutput',
                    required=True,
                    help='Reference output file')
parser.add_argument('-b', '--binaryoutput',
                    default="",
                    help='Binary output file; compare with md5')
parser.add_argument('-l', '--logdir',
                    default="",
                    help='Reference output file')
parser.add_argument('-o', '--debugport',
                    default=10000,
                    help='Debug port')
parser.add_argument('-s', '--sortoutput',
                    action="store_true",
                    help='Sorts output before comparison')
parser.add_argument('-e', '--erroroutput',
                    action="store_true",
                    help='Compare stderr outputs, not stdout')
parser.add_argument('-f', '--injecteflags',
                    action="store_true",
                    help='Inject into EFLAGS register')
parser.add_argument('-x', '--onlytsx',
                    action="store_true",
                    help='Inject into TSX-covered parts only')

parser.add_argument('--limit',
                    default=10,
                    help='Number of fault injection runs')
parser.add_argument('--timeout',
                    default=300,
                    help='Timeout of one run in seconds')

parser.add_argument('--gdb',
                    default='',
                    help='gdb executable')
parser.add_argument('--sde',
                    default='',
                    help='sde executable')


def main():
    print("Changing ptrace_scope to 0...")
    subprocess.call('echo 0 | sudo tee /proc/sys/kernel/yama/ptrace_scope > /dev/null', shell=True)

    args = parser.parse_args()

    global SORTOUTPUT
    if args.sortoutput:
        SORTOUTPUT = True

    global ERROROUTPUT
    if args.erroroutput:
        ERROROUTPUT = True

    if args.injecteflags:
        global SUPPORTED_GP_REGS
        SUPPORTED_GP_REGS.append('rflags')  # look for rflags in the trace

    global GDB
    if args.gdb != "":
        GDB = args.gdb

    global SDE
    if args.sde != "":
        SDE = args.sde

    global ONLYTSX
    if args.onlytsx:
        ONLYTSX = True

    global LIMIT
    if args.limit != "":
        LIMIT = int(args.limit)

    global TIMEOUT
    if args.timeout != "":
        TIMEOUT = int(args.timeout)

    global FULLLOG
    FULLLOG = os.path.basename(args.program) + '.log'

    global LOGDIR
    if args.logdir != "":
        LOGDIR = args.logdir

    global DEBUGPORT
    if args.debugport != "":
        DEBUGPORT = int(args.debugport)

    global binary_output
    if args.binaryoutput != "":
        binary_output = args.binaryoutput
        if SORTOUTPUT:
            assert 0, "--sortoutput and --binaryoutput make no sense together!"

    global ref_output
    if binary_output != "":
        # ref output as binary, calculate md5 sum
        ref_output = hashlib.md5(open(args.refoutput, 'rb').read()).hexdigest()
    else:
        # ref output as normal text, read all lines and sort if needed
        with open(args.refoutput, "r") as f:
            ref_output = f.read()
            if SORTOUTPUT:
                tmplist = ref_output.splitlines(True)
                tmplist.sort()
                ref_output = ''.join(tmplist)
    assert(ref_output != "")

    try:
        os.makedirs("%s/%s" % (LOGDIR, os.path.dirname(args.program)))
    except:
        pass
#        assert(0)

    initLog(args.refoutput, args.dyntrace, args.rtmmode, args.program, args.arguments)
    identifyInsts(args.dyntrace)
    for i in range(0, LIMIT):
        injectFault(args.rtmmode, i, args.program, args.arguments)

if __name__ == "__main__":
    main()

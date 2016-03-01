#!/bin/bash
# first argument: name of benchmark (e.g., 'blackscholes')

source $1/params.sh

# 1: native version
python -u fi-gdb.py $FIGDBARGS -m nop  -p $1/$1.tx.exe -a "$ARGS" -d $1/$1.tx.log -l logs/native/$FIGDBRUN
# 2: ilr version
python -u fi-gdb.py $FIGDBARGS -m nop  -p $1/$1.haft.exe  -a "$ARGS" -d $1/$1.haft.log  -l logs/ilr/$FIGDBRUN
# 3: haft version
python -u fi-gdb.py $FIGDBARGS -m full -p $1/$1.haft.exe  -a "$ARGS" -d $1/$1.haft.log  -l logs/haft/$FIGDBRUN

# additional arguments for fi-gdb.py
FIGDBARGS="$FIGDBARGS -b tmp/${FIGDBRUN}/blackscholes_result.txt -r blackscholes/goldenrun.log"
# arguments for executable
ARGS=" 2 blackscholes/input/in_4.txt tmp/${FIGDBRUN}/blackscholes_result.txt"

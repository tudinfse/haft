# additional arguments for fi-gdb.py
FIGDBARGS="$FIGDBARGS -b tmp/${FIGDBRUN}/ferret_output.txt -r ferret/goldenrun.log"
# arguments for executable
ARGS=" ferret/input/corel lsh ferret/input/queries 5 5 1 tmp/${FIGDBRUN}/ferret_output.txt 2"


# additional arguments for fi-gdb.py
FIGDBARGS="$FIGDBARGS -b tmp/${FIGDBRUN}/dedup_result.dat -r dedup/goldenrun.dat"
# arguments for executable
ARGS=" -c -p -v -t 2 -i dedup/input/key_file.txt -o tmp/${FIGDBRUN}/dedup_result.dat"

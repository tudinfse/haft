# additional arguments for fi-gdb.py
FIGDBARGS="$FIGDBARGS -b tmp/${FIGDBRUN}/eledream.264 -r x264/goldenrun.264"
# arguments for executable
ARGS=" --quiet --qp 20 --partitions b8x8,i4x4 --ref 5 --direct auto --b-pyramid --weightb --mixed-refs --no-fast-pskip --me umh --subme 7 --analyse b8x8,i4x4 --threads 2 -o tmp/${FIGDBRUN}/eledream.264 x264/input/eledream_32x18_1.y4m"

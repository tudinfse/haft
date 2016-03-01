# additional arguments for fi-gdb.py
FIGDBARGS="$FIGDBARGS -b tmp/${FIGDBRUN}/vips_output.v -r vips/goldenrun.v"
# arguments for executable
ARGS=" im_benchmark vips/input/barbados_256x288.v tmp/${FIGDBRUN}/vips_output.v"

export IM_CONCURRENCY=2

set pagination off
set confirm off
set print thread-events off
set breakpoint pending on
set libthread-db-search-path /nonexistent
set debug-file-directory /home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles/native/work/merge4/debug
handle SIGPIPE nostop noprint pass
break brw_opt_combine_constants
commands
silent
printf "CCPROBE alloc=%u end_ip=%d simd=%u name=%s\n", s.alloc.count, s.cfg->blocks[s.cfg->num_blocks-1]->end_ip, s.dispatch_width, ((nir_function*)s.nir->functions.head_sentinel.next)->name
continue
end
run
quit

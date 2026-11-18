set pagination off
set confirm off
set breakpoint pending on
break kumquat.rs:57
commands
silent
if event.hung_up
  set variable event.readable = false
  printf "SIMULATED_HUP_ONLY\n"
end
continue
end
break kumquat.rs:62
commands
silent
printf "HUP_ONLY_BRANCH\n"
continue
end
run

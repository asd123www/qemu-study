# disable cpu scaling.
for cpu in /sys/devices/system/cpu/cpu[0-31]*; do
  # Loop over each idle state for the current CPU
  for state in $cpu/cpuidle/state[0-9]*; do
    echo 1 | sudo tee $state/disable
  done
done
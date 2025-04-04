# check with 'cat /proc/cpuinfo | grep "MHz"'
# Disable hyper threading
if [ "$(cat /sys/devices/system/cpu/smt/control)" == "on" ]; then
    sudo bash -c "echo off > /sys/devices/system/cpu/smt/control"
fi

if [ ! -e "/sys/devices/system/cpu/intel_pstate" ] ||
   [ "$(cat /sys/devices/system/cpu/intel_pstate/status)" == "off" ]; then
    printf "Intel pstate is not the CPU frequency scaling driver. Please disable CPU frequency scaling manually.\n"
    exit 1
fi

# Silent pushd and popd
pushd () {
    command pushd "$@" > /dev/null
}
popd () {
    command popd "$@" > /dev/null
}

pushd /sys/devices/system/cpu
for CORE in cpu[0-111]*; do
    pushd $CORE

    if [ -e "online" ] && [ "$(cat online)" == "0" ]; then
        popd
        continue
    fi

    pushd cpufreq
    sudo bash -c "echo \"performance\" > scaling_governor"
    sudo bash -c "echo $(cat cpuinfo_max_freq) > scaling_max_freq"
    sudo bash -c "echo $(cat cpuinfo_max_freq) > scaling_min_freq"
    popd

    popd
done
popd


pushd /sys/devices/system/cpu/intel_pstate
sudo bash -c "echo 1 > no_turbo"
sudo bash -c "echo 100 > max_perf_pct"
sudo bash -c "echo 100 > min_perf_pct"
popd

for cpu in /sys/devices/system/cpu/cpu[0-111]*; do
  # Loop over each idle state for the current CPU
  for state in $cpu/cpuidle/state[0-9]*; do
    echo 1 | sudo tee $state/disable
  done
done
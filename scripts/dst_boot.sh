# update the kernel image
source config.txt

if [ -z "$1" ]; then
  echo "CPU number not defined"
  exit 1
else
  echo "# of vCPUs: $1"
fi

if [ -z "$2" ]; then
  echo "Memory size not defined"
  exit 1
else
  echo "Memory size: $2"
fi

# start the VM
./scripts/vm-boot/incoming.exp ./linux-$KERNEL_VER $SHARED_STORAGE $1 $2
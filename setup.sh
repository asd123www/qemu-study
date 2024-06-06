sudo apt update

# qemu dependency.
sudo apt-get install linux-generic libelf-dev socat redis-server redis libboost-all-dev pip -y
pip install redis
sudo apt install git libglib2.0-dev libfdt-dev libpixman-1-dev zlib1g-dev python3-venv ninja-build flex bison debootstrap -y

# recommended.
sudo apt-get install git-email -y
sudo apt-get install libaio-dev libbluetooth-dev libcapstone-dev libbrlapi-dev libbz2-dev -y
sudo apt-get install libcap-ng-dev libcurl4-gnutls-dev libgtk-3-dev -y
sudo apt-get install libibverbs-dev libjpeg8-dev libncurses5-dev libnuma-dev -y
sudo apt-get install librbd-dev librdmacm-dev libsasl2-dev libsdl2-dev libseccomp-dev libsnappy-dev libssh-dev -y
sudo apt-get install libvde-dev libvdeplug-dev libvte-2.91-dev libxen-dev liblzo2-dev valgrind xfslibs-dev libnfs-dev libiscsi-dev expect -y

sudo git submodule init
sudo git submodule update
cd apps/stress-ng
sudo make -j 4
sudo make install
cd ../..

# check kvm support.
if [ $(egrep -c '(vmx|svm)' /proc/cpuinfo) -gt 0 ]; then
    echo "KVM is supported"
else 
    echo "KVM is supported"
    exit 1
fi

if lsmod | grep kvm; then 
    echo "KVM module loaded"
else 
    # sudo modprobe kvm-intel
    # sudo modprobe kvm-amd
    echo "KVM module not loaded"
fi

cd qemu-master
./configure --target-list=x86_64-softmmu --enable-kvm --enable-slirp
make -j
sudo make install
cd ..

sudo apt install libslirp0 -y

# compile Linux code.
KERNEL_VER=5.10.54
echo "Use Linux-$KERNEL_VER"
wget https://cdn.kernel.org/pub/linux/kernel/v5.x/linux-$KERNEL_VER.tar.xz
tar xvf linux-$KERNEL_VER.tar.xz
cd linux-$KERNEL_VER
make defconfig
make kvm_guest.config
CONFIG_KVM_GUEST=y
CONFIG_HAVE_KVM=y
CONFIG_PTP_1588_CLOCK_KVM=y
make olddefconfig
./scripts/config -e MEMCG
make -j
cd ..

# creating an image for the kernelPermalink.
sudo apt-get install debootstrap
# cd kernel-image
# chmod +x create-image.sh
# sudo ./create-image.sh

# setup the network bridge for public VM IP address.
sudo ip link add br0 type bridge
sudo ip link set br0 up
sudo ip link set ens1f1 master br0
ip_addr=$(ip addr show ens1f1 | grep "inet\b" | awk '{print $2}' | cut -d/ -f1)
sudo ip addr flush dev ens1f1 
sudo ip addr add $ip_addr/24 brd + dev br0
# tap0 for src, tap1 for dst.
sudo ip tuntap add dev tap0 mode tap
sudo ip link set dev tap0 up
sudo ip link set tap0 master br0
sudo ip tuntap add dev tap1 mode tap
sudo ip link set dev tap1 up
sudo ip link set tap1 master br0

# disable nic adaptive batching.
sudo ethtool -C ens1f1 adaptive-rx off adaptive-tx off rx-frames 1 rx-usecs 0  tx-frames 1 tx-usecs 0
sudo ethtool -C ens1f1 adaptive-rx off adaptive-tx off rx-frames 1 rx-usecs 0  tx-frames 1 tx-usecs 0
sudo ethtool -C tap0 adaptive-rx off adaptive-tx off rx-frames 1 rx-usecs 0  tx-frames 1 tx-usecs 0
sudo ethtool -C tap1 adaptive-rx off adaptive-tx off rx-frames 1 rx-usecs 0  tx-frames 1 tx-usecs 0
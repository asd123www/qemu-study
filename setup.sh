sudo apt update

# qemu dependency.
sudo apt-get install linux-generic libelf-dev socat -y
sudo apt install git libglib2.0-dev libfdt-dev libpixman-1-dev zlib1g-dev python3-venv ninja-build flex bison debootstrap -y

# recommended.
sudo apt-get install git-email -y
sudo apt-get install libaio-dev libbluetooth-dev libcapstone-dev libbrlapi-dev libbz2-dev -y
sudo apt-get install libcap-ng-dev libcurl4-gnutls-dev libgtk-3-dev -y
sudo apt-get install libibverbs-dev libjpeg8-dev libncurses5-dev libnuma-dev -y
sudo apt-get install librbd-dev librdmacm-dev -y
sudo apt-get install libsasl2-dev libsdl2-dev libseccomp-dev libsnappy-dev libssh-dev -y
sudo apt-get install libvde-dev libvdeplug-dev libvte-2.91-dev libxen-dev liblzo2-dev -y
sudo apt-get install valgrind xfslibs-dev -y
sudo apt-get install libnfs-dev libiscsi-dev -y


# check kvm support
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
./configure --target-list=x86_64-softmmu --enable-kvm
make -j
sudo make install
cd ..

# compile Linux code.
KERNEL_VER=5.10.54
echo "Use Linux-$KERNEL_VER"
wget https://cdn.kernel.org/pub/linux/kernel/v5.x/linux-$KERNEL_VER.tar.xz
tar xvf linux-$KERNEL_VER.tar.xz
cd linux-$KERNEL_VER
make defconfig
make kvmconfig
CONFIG_KVM_GUEST=y
CONFIG_HAVE_KVM=y
CONFIG_PTP_1588_CLOCK_KVM=y
make olddefconfig
make -j
cd ..

# Creating an image for the kernelPermalink
sudo apt-get install debootstrap
cd kernel-image
chmod +x create-image.sh
sudo ./create-image.sh

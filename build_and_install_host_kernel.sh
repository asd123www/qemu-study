# !/bin/bash
set -eux -o pipefail

# Enable source
printf "Installing dependencies...\n"
sudo cp /etc/apt/sources.list /etc/apt/sources.list~
sudo sed -Ei 's/^# deb-src /deb-src /' /etc/apt/sources.list
sudo apt-get update

# Install build dependencies
sudo apt --fix-broken install -y
sudo apt-get build-dep linux linux-image-$(uname -r) -y || true
sudo apt-get install -y libncurses-dev flex dwarves bison openssl zstd libssl-dev dkms \
    libelf-dev libudev-dev libpci-dev libiberty-dev \
    autoconf fakeroot bc cpio rsync --allow-change-held-packages

SCRIPT_PATH=`realpath $0`
BASE_DIR=`dirname $SCRIPT_PATH`
LINUX_PATH="$BASE_DIR/linux"

pushd $LINUX_PATH
if [ ! -e "Makefile" ]; then
    git submodule init
    git submodule update
fi

# Cleanup the previous build
rm -f ../linux-* 2> /dev/null
make distclean

# Configure kernel
printf "Configuring kernel...\n"
cp /boot/config-$(uname -r) .config
(yes "" || true) | make localmodconfig
./scripts/config -e CONFIG_DAMON
./scripts/config -e CONFIG_DAMON_VADDR
./scripts/config -e CONFIG_DAMON_PADDR
./scripts/config -e CONFIG_DAMON_SYSFS
./scripts/config -e CONFIG_DAMON_DBGFS
./scripts/config -e CONFIG_DAMON_RECLAIM
./scripts/config -e CONFIG_BRIDGE
./scripts/config -e CONFIG_BRIDGE_NETFILTER
./scripts/config -e CONFIG_NETFILTER_XTABLES
./scripts/config -e CONFIG_NF_CONNTRACK
./scripts/config -e CONFIG_IP_NF_IPTABLES
./scripts/config -e CONFIG_IP_NF_FILTER
./scripts/config -m CONFIG_OVERLAY_FS
./scripts/config -m CONFIG_XFRM_USER
./scripts/config -m CONFIG_INET_ESP
./scripts/config -m CONFIG_IPV6_TUNNEL
./scripts/config -m CONFIG_NET_KEY
./scripts/config -m CONFIG_NF_CT_NETLINK
./scripts/config -e CONFIG_IP_NF_TARGET_MASQUERADE
./scripts/config -e CONFIG_NETFILTER_XT_MATCH_ADDRTYPE
./scripts/config -m CONFIG_NETFILTER_XT_MATCH_CONNTRACK
./scripts/config -m CONFIG_NF_TABLES
./scripts/config -m CONFIG_NFT_NAT
./scripts/config -m CONFIG_NFT_COMPAT
./scripts/config -m CONFIG_VETH
./scripts/config -m CONFIG_VHOST_NET

./scripts/config -e CONFIG_IP_NF_NAT
./scripts/config -e CONFIG_NF_NAT
./scripts/config -m CONFIG_NF_CONNTRACK_EVENTS
./scripts/config -d SYSTEM_REVOCATION_KEYS
./scripts/config -d SYSTEM_TRUSTED_KEYS
./scripts/config -d CONFIG_MODULE_SIG
./scripts/config -e CONFIG_MODULE_COMPRESS_NONE
./scripts/config -d CONFIG_MODULE_COMPRESS_ZSTD
make olddefconfig
if [ -z "$(cat .config | grep CONFIG_DAMON)" ]; then
    printf "Cannot find CONFIG_DAMON in .config file. Please enable it manually by 'make nconfig'.\n"
    exit 1
fi
if [ -z "$(cat .config | grep CONFIG_DAMON_VADDR)" ]; then
    printf "Cannot find CONFIG_DAMON_VADDR in .config file. Please enable it manually by 'make nconfig'.\n"
    exit 1
fi
if [ -z "$(cat .config | grep CONFIG_DAMON_PADDR)" ]; then
    printf "Cannot find CONFIG_DAMON_PADDR in .config file. Please enable it manually by 'make nconfig'.\n"
    exit 1
fi
if [ -z "$(cat .config | grep CONFIG_DAMON_SYSFS)" ]; then
    printf "Cannot find CONFIG_DAMON_SYSFS in .config file. Please enable it manually by 'make nconfig'.\n"
    exit 1
fi
# if [ -z "$(cat .config | grep CONFIG_DAMON_DBGFS)" ]; then
#     printf "Cannot find CONFIG_DAMON_DBGFS in .config file. Please enable it manually by 'make nconfig'.\n"
#     exit 1
# fi
if [ -z "$(cat .config | grep CONFIG_DAMON_RECLAIM)" ]; then
    printf "Cannot find CONFIG_DAMON_RECLAIM in .config file. Please enable it manually by 'make nconfig'.\n"
    exit 1
fi

# Compile kernel
printf "Compiling kernel...\n"
make deb-pkg -j8
popd

# Install kernel
printf "Installing kernel...\n"
pushd $BASE_DIR
sudo dpkg -i linux-*.deb
popd

if [ -z "$(awk -F\' '/menuentry / {print $2}' /boot/grub/grub.cfg | grep -m 1 'Ubuntu, with Linux 5.19.17-fmsync+')" ]; then
    printf "Cannot find the Memstrata kernel. Please install the kernel manually.\n"
    exit 1
fi

printf "Memstrata kernel is installed. To boot into Memstrata kernel, please run:\n"
printf "    sudo grub-reboot \"Advanced options for Ubuntu>Ubuntu, with Linux 5.19.17-fmsync+\"\n"
printf "    sudo reboot\n"

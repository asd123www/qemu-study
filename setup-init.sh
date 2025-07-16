#!/usr/bin/env bash
###############################################################################
#  qemu_study_setup.sh – full setup & workload-launch for github.com/asd123www/qemu-study
#  Tested on CloudLab r650, UBUNTU20-64-STD.  Run as root.
###############################################################################
set -euo pipefail

### ---- USER-ADJUSTABLE PARAMETERS ------------------------------------------------
DISK_DEV="/dev/sdb"          # raw NVMe device
MNT_DIR="/mnt/mynvm"             # mountpoint for repo & shared storage
REPO_PATH="${MNT_DIR}/qemu-study"

SERVER_IP="10.10.1.3"            # this host’s IP (used as src/dst/backup_ip)
VM_IP="10.10.1.100"              # guest VM IP (must match your network plan)
GUEST_KERNEL_VER="5.15"        # value for KERNEL_VER in config.txt
NIC_NAME="enp24s0f0"             # NIC that will host tap devices

SRC_NUMA=0;  DST_NUMA=0;         CXL_NUMA=0
SRC_CPUSET="0-3"; DST_CPUSET="4-7"
NIC_INTERRUPT_CORE=0
VHOST_CORE=1
###############################################################################

msg() { echo -e "\e[1m[$(date +%T)] $*\e[0m"; }



###############################################################################
# 1. One-time disk format & mount
###############################################################################

if ! mountpoint -q "${MNT_DIR}"; then
  msg "Formatting and mounting ${DISK_DEV} → ${MNT_DIR}"
  mkfs.ext4 -F "${DISK_DEV}"
  mkdir -p "${MNT_DIR}"
  mount "${DISK_DEV}" "${MNT_DIR}"
  chmod 777 -R "${MNT_DIR}"

  # >>> ADD THIS LINE <<<
  grep -q "${MNT_DIR}" /etc/fstab || echo "${DISK_DEV} ${MNT_DIR} ext4 defaults,nofail 0 2" >> /etc/fstab
fi

###############################################################################
# 2. Clone study repo (and, if needed, the fmsync kernel)
###############################################################################
if [[ ! -d "${REPO_PATH}" ]]; then
  msg "Cloning qemu-study repo"
  git clone https://github.com/asd123www/qemu-study.git "${REPO_PATH}"
fi
cd "${REPO_PATH}"

###############################################################################
# 3. Ensure host is running the fmsync kernel – build + reboot if necessary
###############################################################################
if [[ "$(uname -r)" != *fmsync* ]]; then
  msg "Building & installing fmsync host kernel"
  git clone https://github.com/asd123www/linux.git linux || true
  sudo bash build_and_install_host_kernel.sh
  msg "Rebooting into Linux 5.19.17-fmsync+ …"
  sudo grub-reboot "Advanced options for Ubuntu>Ubuntu, with Linux 5.19.17-fmsync+"
  sudo reboot
fi

###############################################################################
# 4. Patch config.txt with local settings
###############################################################################
CONFIG_FILE="config.txt"
msg "Patching ${CONFIG_FILE}"
sed -i \
  -e "s/^src_ip=.*/src_ip=${SERVER_IP}/" \
  -e "s/^dst_ip=.*/dst_ip=${SERVER_IP}/" \
  -e "s/^backup_ip=.*/backup_ip=${SERVER_IP}/" \
  -e "s|^SHARED_STORAGE=.*|SHARED_STORAGE=${MNT_DIR}/shared|" \
  -e "s/^KERNEL_VER=.*/KERNEL_VER=${GUEST_KERNEL_VER}/" \
  -e "s/^NIC_NAME=.*/NIC_NAME=${NIC_NAME}/" \
  -e "s/^VM_IP=.*/VM_IP=${VM_IP}/" \
  -e "s/^SRC_NUMA=.*/SRC_NUMA=${SRC_NUMA}/" \
  -e "s/^DST_NUMA=.*/DST_NUMA=${DST_NUMA}/" \
  -e "s/^CXL_NUMA=.*/CXL_NUMA=${CXL_NUMA}/" \
  -e "s/^SRC_CPUSET=.*/SRC_CPUSET=${SRC_CPUSET}/" \
  -e "s/^DST_CPUSET=.*/DST_CPUSET=${DST_CPUSET}/" \
  -e "s/^NIC_INTERRUPT_CORE=.*/NIC_INTERRUPT_CORE=${NIC_INTERRUPT_CORE}/" \
  -e "s/^VHOST_CORE=.*/VHOST_CORE=${VHOST_CORE}/" \
  "${CONFIG_FILE}"

###############################################################################
# 5. System tuning (THP, CPU scaling, pinning helpers)
###############################################################################
msg "Disabling THP, CPU scaling & hyper-threading"
sudo bash scripts/disable_THP.sh always
sudo bash scripts/disable_cpu_scale.sh

###############################################################################
# 6. Build QEMU, helper tools, and prepare guest image
###############################################################################
if [[ ! -d qemu-build ]]; then
  msg "Building qemu-master (branch: debug)"
  sudo bash ./setup.sh debug
fi

if [[ ! -f kernel-image/focal-server-cloudimg-amd64.img ]]; then
  msg "Creating guest VM image"
  ( cd kernel-image && sudo bash create-image.sh )
fi

###############################################################################
# 7. Install customized Redis inside the guest
###############################################################################
if ! grep -q "Ready to accept connections" vm_src.txt 2>/dev/null; then
  msg "Booting VM and installing Redis (may take several minutes)"
  ./apps/controller shm src apps/vm-boot/setup_redis.exp 4 16G vm_src.txt 400000 || {
      msg "setup_redis failed – auto-clean & retry"
      sudo bash scripts/my_kill.sh
      ./apps/controller shm src apps/vm-boot/setup_redis.exp 4 16G vm_src.txt 400000
  }
fi

###############################################################################
# 8. Launch workload & live-migration
###############################################################################
msg "Starting source VM with Redis + background fmsync"
./apps/controller shm src apps/vm-boot/redis.exp 4 20G vm_src.txt 500000 &

sleep 5
msg "Starting destination VM (pre-copy mode)"
./apps/controller qemu-precopy dst 4 20G vm_dst.txt 1342177280B &

sleep 5
msg "Launching backup fmsync for 30 s"
./apps/controller shm backup 30 &

sleep 5
msg "Triggering cut-over (SIGUSR1 to controller)"
sudo kill -SIGUSR1 "$(cat controller.pid)"

msg "=== All steps complete. ==="
echo "Run YCSB on another host against Redis @ ${VM_IP} to generate load."

# Virtual Machine Migration Study

This works in cloudlab r650 server, with UBUNTU20-64-STD.

Attach the disk:

		sudo mkfs.ext4 /dev/nvme0n1
		sudo mkdir /mnt/mynvm
		sudo mount /dev/nvme0n1 /mnt/mynvm/
		sudo chmod 777 -R /mnt/mynvm/


Clone this repo(`git clone git@github.com:asd123www/qemu-study.git`) into `/mnt/mynvm`, and enter the repo.

First, load fmsync kernel, you can also do this in the cloudlab persistent storage:

	  git clone git@github.com:asd123www/linux.git
	  sudo bash build_and_install_host_kernel.sh
      sudo grub-reboot "Advanced options for Ubuntu>Ubuntu, with Linux 5.19.17-fmsync+"
	  sudo reboot

Before installing the workloads, check `config.txt`: You may(only) need to modify `src/dst/backup_ip` to the server's ip. `SHARED_STORAGE` is the directory path. `KERNEL_VER` is the guestOS's kernel version. `NIC_NAME` is the NIC that we install tap device for VMs.  `VM_IP` is the VM's assigned ip address. `[MIGRATION_PORT/SRC_CONTROL_PORT/DST_CONTROL]_PORT` is the port our control program uses. `[SRC/DST]_NUMA` is the numa node source/dest VM uses, `CXL_NUMA` is the cxl numa. `[SRC/DST]_CPUSET` is the CPU cores that the source/dest VM use, must list all cores like `core1,core2,...`. `NIC_INTERRUPT_CORE` is where we pin all interrupts. `VHOST_CORE` is where we pin all the vhost threads, `MIGRATION_CORE` is ignored now. `META_STATE_LENGTH, HOT_PAGE_STATE_LENGTH` is how many bytes the metadata in VM image we use.

To disable/enable THP: `sudo bash scripts/disable_THP.sh [never/always]`. To disable cpu frequency scaling and hyperthreading: `sudo bash scripts/disable_cpu_scale.sh`. To pin vhost threads: `sudo bash scripts/pin_vhost.sh`. To pin VM threads: `sudo bash scripts/pin_vm_to_cores.sh [src/dst]`.

Then, setup the qemu and workloads: `sudo bash scripts/setup.sh debug`. The arg is which [qemu-master](git@github.com:asd123www/qemu-master.git) branch to use, debug is the latest version, but has the inconsistency bug. Then prepare the VM image: `cd kernel-image`, then `sudo bash create-image.sh`.

To install our customized redis: `./apps/controller shm src apps/vm-boot/setup_redis.exp 4 16G vm_src.txt 400000`, wait until the output(`cat vm_src.txt`) in vm_src.txt shows it successfully installed. You can also just `ssh root@10.10.1.100` to run commands.

After this, you can run the migration:

	# ./apps/controller shm src apps/vm-boot/redis.exp {vcpus} {memory} {vm_path} {fmsync frequency in us}
	./apps/controller shm src apps/vm-boot/redis.exp 4 20G vm_src.txt 500000
	./apps/controller qemu-precopy dst 4 20G vm_dst.txt 1342177280B
	./apps/controller shm backup 30 # run fmsync in background for 30s.
	sudo kill -SIGUSR1 $(cat controller.pid) # start the fmsync.
This example command will start a redis-server in the VM. To generate workloads, you need another server to run ycsb, with server ip `10.10.1.100`(VM_IP). You can also measure the redis's fmsync background overhead, check `auto-bench/write-through/redis_write_through.py`, it contains all the setups to test fmsync.

The background fmsync logic is [ram_save_iterate_shm](https://github.com/asd123www/qemu-master/blob/2ad5de1bd992dfd55bec98e62a4ca5937f9ed2a5/migration/ram.c#L3461), this function is called every {fmsync frequency in us}. The last iteration is [ram_save_complete_shm](https://github.com/asd123www/qemu-master/blob/2ad5de1bd992dfd55bec98e62a4ca5937f9ed2a5/migration/ram.c#L3627C12-L3627C33). Those functions call `fmsync_memory_dirty_log_huge`, in the end calls into the kernel: [kvm_vm_ioctl](https://github.com/asd123www/linux/blob/59a3f198148fe25049d9929c1a01392fe56169dc/virt/kvm/kvm_main.c#L4630C1-L4650C1), [kvm_tdp_mmu_fmsync_dirty_log](https://github.com/asd123www/linux/blob/59a3f198148fe25049d9929c1a01392fe56169dc/arch/x86/kvm/mmu/tdp_mmu.c#L1750).
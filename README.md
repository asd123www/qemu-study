# Virtual Machine Migration Study

The framework is QEMU+KVM. My current understanding is QEMU emulates all the devices in software. KVM can provide native CPU virtualization support instead of QEMU dynamic translation, we can have a better performance.

The experiment is based on the `xl170` machine in cloudlab with `kernel 5.4.0-164-generic`. To install qemu, you can simply run: `sudo bash setup.sh`. After this, we will get the kernel image, check this [blog](https://vccolombo.github.io/cybersecurity/linux-kernel-qemu-setup/) for more.Then, in the source machine, you can boot up a Linux kernel by:

```
sudo qemu-system-x86_64 \
		--enable-kvm \
		-m 4G \
		-kernel ./linux-5.10.54/arch/x86_64/boot/bzImage \
		-nographic -serial mon:stdio \
		-drive file=./kernel-image/bullseye.img,format=raw \
		-append "console=ttyS0 root=/dev/sda" \
		-monitor unix:qemu-monitor-migration,server,nowait
```

The `-monitor` option creates a unix socket file for further monitoring the VM. You can connect to the VM monitor via `sudo socat stdio unix-connect:qemu-monitor-migration`. Check [this](https://unix.stackexchange.com/questions/426652/connect-to-running-qemu-instance-with-qemu-monitor) for more details. Then you can boot up a **same** Linux kernel waiting migration by:

```
sudo qemu-system-x86_64 \
		--enable-kvm \
		-m 4G \
		-kernel ./linux-5.10.54/arch/x86_64/boot/bzImage \
		-nographic -serial mon:stdio \
		-drive file=./kernel-image/bullseye.img,format=raw \
		-append "console=ttyS0 root=/dev/sda" \
		-incoming tcp:0:4444
```

The `-incoming` option specifies the listening TCP port. I didn't explore other transport protocols. 

Now both the source and dest machines are ready for the migration. We can issue the migration by `migrate -d tcp:10.10.1.2:4444` in the monitor.


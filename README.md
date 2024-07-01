# Virtual Machine Migration Study

The framework is QEMU+KVM. My current understanding is that QEMU emulates all the devices in software. KVM can provide native CPU virtualization support instead of QEMU dynamic translation, so we can have a better performance.

The experiment is based on the `xl170` machine in Cloudlab with `kernel 5.4.0-164-generic`. To install QEMU, you can simply run: `sudo bash setup.sh`. After this, we will get the kernel image, check this [blog](https://vccolombo.github.io/cybersecurity/linux-kernel-qemu-setup/) for more. Remember to copy the image to the shared storage, in Cloudlab the directory is `/proj/your-group-name`. Then, in the source machine, you can boot up a Linux kernel by:

```
sudo qemu-system-x86_64 \
		--enable-kvm \
		-cpu host \
		-smp 2 \
		-m 256M \
		-kernel ./linux-5.10.54/arch/x86_64/boot/bzImage \
		-nographic -serial mon:stdio \
		-drive file=/proj/xdp-PG0/bullseye.img,format=raw \
		-append "console=ttyS0 root=/dev/sda earlyprintk=serial net.ifnames=0" \
		-netdev user,id=net0,hostfwd=tcp:127.0.0.1:10021-:22 \
		-device e1000,netdev=net0 \
		-netdev tap,id=ens1f0,ifname=tap0,script=no,downscript=no \
		-device virtio-net,netdev=ens1f0,mac=52:55:00:d1:55:01 \
		-monitor unix:qemu-monitor-migration,server,nowait
```

The `-monitor` option creates a Unix socket file for further monitoring the VM. You can connect to the VM monitor via `sudo socat stdio unix-connect:qemu-monitor-migration`. Check [this](https://unix.stackexchange.com/questions/426652/connect-to-running-qemu-instance-with-qemu-monitor) for more details. Then you can boot up a **same** Linux kernel waiting for migration by:

```
sudo qemu-system-x86_64 \
		--enable-kvm \
		-cpu host \
		-smp 2 \
		-m 256M \
		-kernel ./linux-5.10.54/arch/x86_64/boot/bzImage \
		-nographic -serial mon:stdio \
		-drive file=/proj/xdp-PG0/bullseye.img,format=raw \
		-append "console=ttyS0 root=/dev/sda earlyprintk=serial net.ifnames=0" \
		-netdev user,id=net0,hostfwd=tcp:127.0.0.1:10021-:22 \
		-device e1000,netdev=net0 \
		-netdev tap,id=ens1f0,ifname=tap0,script=no,downscript=no \
		-device virtio-net,netdev=ens1f0,mac=52:55:00:d1:55:01 \
		-incoming defer \
		-monitor unix:qemu-monitor-migration,server,nowait
```

The `-incoming` option specifies the listening TCP port. I didn't explore other transport protocols. 

Now both the source and dest machines are ready for the migration. We can issue the migration by `migrate -d tcp:10.10.1.2:4444` in the monitor. Check this [blog](https://wiki.gentoo.org/wiki/QEMU/Options) for more options. You can check how many bytes are transferred by `sudo iftop -i ens1f1`.

Because we need to profile the effect of live migration, we should try to keep the network connections alive. This requires the VM to expose its internal IP address as public, instead of using the host IP address. One way to do this is [Linux TAP/Bridges](https://blog.stefan-koch.name/2020/10/25/qemu-public-ip-vm-with-tap). After booting the kernel, we should assign an IP address manually by: `ip addr add 10.10.1.100/24 dev eth0`. Then you can test the connectivity by `iperf` tool.

## QEMU concepts

When I was studying the `socket_start_outgoing_migration` function, I found the control flow would go back to the entrance of migration and exit. QEMU will invoke `socket_outgoing_migration` after building a connection with the destination. This reminded me of event-driven programming. Check this [blog](https://blog.vmsplice.net/2020/08/qemu-internals-event-loops.html) for more.

Warning: if you want to do VM migration inside one server, you should disable port forwarding!
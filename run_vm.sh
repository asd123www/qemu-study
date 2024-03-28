# boot up the source VM.
sudo qemu-system-x86_64 \
		--enable-kvm \
		-m 4G \
		-kernel ./linux-5.10.54/arch/x86_64/boot/bzImage \
		-nographic -serial mon:stdio \
		-drive file=./kernel-image/bullseye.img,format=raw \
		-append "console=ttyS0 root=/dev/sda" \
		-monitor unix:qemu-monitor-migration,server,nowait

# boot up the dest VM.
sudo qemu-system-x86_64 \
		--enable-kvm \
		-m 4G \
		-kernel ./linux-5.10.54/arch/x86_64/boot/bzImage \
		-nographic -serial mon:stdio \
		-drive file=./kernel-image/bullseye.img,format=raw \
		-append "console=ttyS0 root=/dev/sda" \
		-incoming tcp:0:4444
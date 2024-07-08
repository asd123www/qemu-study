# update the kernel image
cd kernel-image
chmod +x create-image.sh
sudo bash create-image.sh
rm -f /proj/xdp-PG0/bullseye.img
cp bullseye.img /proj/xdp-PG0/
cd ..

chmod +x boot_vm.exp
./boot_vm.exp
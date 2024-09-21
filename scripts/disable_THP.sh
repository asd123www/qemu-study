sudo sh -c "echo never | tee /sys/kernel/mm/transparent_hugepage/enabled"
sudo sh -c "echo never | tee /sys/kernel/mm/transparent_hugepage/defrag"
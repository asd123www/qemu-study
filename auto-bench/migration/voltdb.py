from time import sleep
import fabric
import sys

def run_async(node, path, command):
    with node.cd(path):
        return node.run(command, asynchronous = True, pty = True) # non-blocking

def run_sync(node, path, command):
    with node.cd(path):
        node.run(command, asynchronous = False, pty = True)

root_dir = "/mnt/mynvm/qemu-study"

def bench(mode, client_output, duration):
    write_through_duration = 30

    src_command = f"./apps/controller {mode} src apps/vm-boot/voltdb.exp 4 9G vm_src.txt {duration} > ctl_src.txt"
    dst_command = f"./apps/controller {mode} dst 4 9G vm_dst.txt > ctl_dst.txt"
    backup_command = f"./apps/controller {mode} backup {write_through_duration} > ctl_backup.txt"

    print(src_command)
    ret1 = run_async(src, root_dir, src_command)
    ret2 = run_async(dst, root_dir, dst_command)
    ret3 = run_async(backup, root_dir, backup_command)
    sleep(80)
    run_sync(src, root_dir, "sudo bash scripts/pin_vm_to_cores.sh src")
    sleep(3)

    client_init_command = "sudo bash apps/workload_scripts/voltdb/load_tpcc.sh"
    client_run_command = f"sudo bash apps/workload_scripts/voltdb/run_tpcc.sh 80 100 > {client_output}"

    # warmup.
    run_sync(client, root_dir, client_init_command)
    run_sync(client, root_dir, "sudo bash apps/workload_scripts/voltdb/run_tpcc.sh 80 100")

    ssh_command = f"""
        ssh -o "StrictHostKeyChecking no" root@10.10.1.100 << 'ENDSSH'
        echo "Connected to second server"
        # Place your commands here
        cd ~/voltdb/tests/test_apps/tpcc/
        kill -9 $(ps -eaf | grep voltdb | grep -v grep | awk '{{print $2}}')
        nohup bash run.sh > voltdb_log.out 2>&1 &
        sleep 5
        exit
        ENDSSH
        """

    run_sync(src, "~", ssh_command)
    run_sync(client, root_dir, client_init_command)
    run_sync(backup, root_dir, "sudo kill -SIGUSR1 $(cat controller.pid)")
    run_sync(client, root_dir, client_run_command)

    run_sync(src, root_dir, "sudo bash scripts/my_kill.sh")
    run_sync(dst, root_dir, "sudo bash scripts/my_kill.sh")
    run_sync(backup, root_dir, "sudo bash scripts/my_kill.sh")

if __name__ == "__main__":
    src = fabric.Connection(host = "src")
    dst = fabric.Connection(host = "src")
    backup = fabric.Connection(host = "src")
    client = fabric.Connection(host = "dst")

    if len(sys.argv) != 4:
        print("Usage: python script.py <mode:`shm` or `qemu-precopy`> <client_output_path> <duration(us)>")
        sys.exit(1)

    mode = sys.argv[1]
    client_output = sys.argv[2]
    duration = sys.argv[3]

    bench(mode, client_output, duration)

# For voltdb, the sleep interval is 500ms. So just go with `500000`
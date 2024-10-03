from distutils.log import warn
from http import server
from multiprocessing import Process
import pty
from pydoc import cli
from sys import stdout
from threading import Thread
from time import sleep
from unittest import result
import threading
import fabric
import invoke
import sys
# pip install Fabric

def init_machine(node):
    # clone repo from github.
    node.run("rm -rf qemu-study/")
    node.run("git clone git@github.com:asd123www/qemu-study.git")

    # Prepare kernel.
    with node.cd("qemu-study/"):
        node.run("pwd")
        node.run("sudo bash setup.sh")

def kill_machine(node):
    with node.cd("qemu-study/"):
        node.run("sudo bash my_kill.sh")

def control_machine(servers, func):
    for node in servers: func(node)
def control_machine_parallel(servers, func):
    replica_process = []
    for (i, node) in enumerate(servers):
        p = Thread(target = func, args=(node, ))
        p.start()
        replica_process.append(p)
    for p in replica_process: p.join()



def run_async(node, path, command):
    with node.cd(path):
        return node.run(command, asynchronous = True, pty = True) # non-blocking

def run_sync(node, path, command):
    with node.cd(path):
        node.run(command, asynchronous = False, pty = True)


def bench(mode, vm_path, clt_path, duration):
    src_command = f"./apps/controller shm src apps/vm-boot/voltdb.exp 4 9G {vm_path} {duration}"
    client_init_command = "sudo bash apps/workload_scripts/voltdb/load_tpcc.sh"
    client_run_command = f"sudo bash apps/workload_scripts/voltdb/run_tpcc.sh 80 100 > {clt_path}"

    print(src_command)
    ret1 = run_async(src, "/mnt/mynvm/qemu-study", src_command)
    sleep(100)

    # start migration thread.
    if mode == "shm":
        run_sync(src, "/mnt/mynvm/qemu-study", f"echo \"shm_migrate /my_shared_memory 16 {duration}\" | sudo socat stdio unix-connect:qemu-monitor-migration-src")
    
    # warmup.
    run_sync(client, "/mnt/mynvm/qemu-study/", client_init_command)
    run_sync(client, "/mnt/mynvm/qemu-study/", "sudo bash apps/workload_scripts/voltdb/run_tpcc.sh 80 100")

    ssh_command = f"""
        ssh root@10.10.1.100 << 'ENDSSH'
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
    run_sync(client, "/mnt/mynvm/qemu-study/", client_init_command)
    run_sync(client, "/mnt/mynvm/qemu-study/", client_run_command)


if __name__ == "__main__":
    src = fabric.Connection(host = "src")
    client = fabric.Connection(host = "dst")
    
    if len(sys.argv) != 5:
        print("Usage: python script.py <mode> <vm_output_path> <client_output_path> <duration(us)>")
        sys.exit(1)
    
    mode = sys.argv[1]
    vm_path = sys.argv[2]
    clt_path = sys.argv[3]
    duration = sys.argv[4]

    bench(mode, vm_path, clt_path, duration)

# python3 voltdb_write_through.py normal vm_voltdb_normal.txt clt_voltdb_normal.txt 0
# python3 voltdb_write_through.py shm vm_voltdb_shm.txt clt_voltdb_shm.txt 1000
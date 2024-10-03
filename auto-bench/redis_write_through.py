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
    vcpus = 4
    memory = "15G"
    recordcount = 1700000
    operationcount = 20000000

    src_command = f"./apps/controller shm src apps/vm-boot/redis.exp {vcpus} {memory} {vm_path} {duration}"
    client_init_command = f"sudo bash apps/workload_scripts/redis/load_ycsb.sh {vcpus} {recordcount} 8"
    client_run_command = f"sudo bash apps/workload_scripts/redis/run_ycsb.sh {vcpus} {operationcount} 8 > {clt_path}"

    print(src_command)
    ret1 = run_async(src, "/mnt/mynvm/qemu-study", src_command)
    sleep(100)

    # start migration thread.

    if mode == "shm":
        run_sync(src, "/mnt/mynvm/qemu-study", f"echo \"shm_migrate /my_shared_memory 16 {duration}\" | sudo socat stdio unix-connect:qemu-monitor-migration-src")

    # warmup.
    run_sync(client, "/mnt/mynvm/qemu-study/", client_init_command)
    run_sync(client, "/mnt/mynvm/qemu-study/", f"sudo bash apps/workload_scripts/redis/run_ycsb.sh {vcpus} {operationcount} 8")

    # experimental result.
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

# python3 redis_write_through.py shm vm_redis_shm.txt clt_redis_shm.txt 1000
# python3 redis_write_through.py normal vm_redis_normal.txt clt_redis_normal.txt 0
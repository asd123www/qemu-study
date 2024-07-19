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
# pip install Fabric
import multiprocessing


COUNT = 0
DEV = 'ens1f1'

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



def run_async(node, path, command, watcher):
    with node.cd(path):
        return node.run(command, asynchronous = True, pty = True, watchers=[watcher]) # non-blocking

def run_sync(node, path, command, watcher):
    with node.cd(path):
        node.run(command, asynchronous = False, pty = True, watchers=[watcher])


def bench(path, algo, vcpus, memory, bandwidth):
    src_command = f"./apps/controller {algo} src scripts/vm-boot/redis.exp {vcpus} {memory} vm_src_out.txt {bandwidth} > ctl_src.txt"
    dst_command = f"./apps/controller {algo} dst {vcpus} {memory} vm_dst_out.txt {bandwidth} > ctl_dst.txt"
    backup_command = f"./apps/controller {algo} backup > ctl_backup.txt"


    ret1 = run_async(src, "zezhou/qemu-study", src_command, sudo_pass)
    ret2 = run_async(dst, "zezhou/qemu-study", dst_command, sudo_pass)
    ret3 = run_async(backup_ctl, "zezhou/qemu-study", backup_command, sudo_pass)
    sleep(80)

    ycsb_command = "taskset -c 25-31 ./ycsbc -db redis -threads 6 -P ./workloads/workloada.spec  -host 172.17.0.100 -port 12345 -slaves 0"
    run_sync(backup, "zezhou/qemu-study/apps/redis/YCSB-C-master", ycsb_command, sudo_pass)

    # # stop all running VM & controller.
    run_sync(backup_ctl, "zezhou/qemu-study", "sudo bash scripts/my_kill.sh", sudo_pass)
    run_sync(src, "zezhou/qemu-study", "sudo bash scripts/my_kill.sh", sudo_pass)
    run_sync(dst, "zezhou/qemu-study", "sudo bash scripts/my_kill.sh", sudo_pass)

    # collect the result.
    src.get("/home/inteluser/zezhou/qemu-study/vm_src_out.txt", f"{path}/vm_src_out.txt")
    src.get("/home/inteluser/zezhou/qemu-study/ctl_src.txt", f"{path}/ctl_src.txt")
    dst.get("/home/inteluser/zezhou/qemu-study/vm_dst_out.txt", f"{path}/vm_dst_out.txt")
    dst.get("/home/inteluser/zezhou/qemu-study/ctl_dst.txt", f"{path}/ctl_dst.txt")
    backup.get("/home/inteluser/zezhou/qemu-study/ctl_backup.txt", f"{path}/ctl_backup.txt")
    backup.get("/home/inteluser/zezhou/qemu-study/apps/redis/YCSB-C-master/redis_result.txt", f"{path}/redis_result.txt")


if __name__ == "__main__":
    user = "inteluser"
    password = "Archer.123$"
    config = fabric.Config(overrides={'sudo': {'password': password}})
    sudo_pass = invoke.Responder(pattern=r'\[sudo\] password for {0}:'.format(user), response=password + '\n')

    # Host cxl
    #     HostName=epsi-archercity
    #     User=inteluser
    #     ProxyJump=rohank@ssh.intel-research.net
    src = fabric.Connection(host = "cxl", config=config)
    dst = fabric.Connection(host = "cxl", config=config)
    backup = fabric.Connection(host = "cxl", config=config)
    backup_ctl = fabric.Connection(host = "cxl", config=config)


    # bench("qemu-precopy", "4", "8G", "1342177280B")
    # bench("qemu-postcopy", "4", "8G", "1342177280B")
    bench("tmp", "shm", "4", "8G", "")

    print("Finish benching successfully.")
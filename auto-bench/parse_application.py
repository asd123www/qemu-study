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
import os
import re



def redis_parser(file_path):
    print(file_path)
    pattern = r"\[client (\d+)\] \[OVERALL\], Throughput\(ops/sec\), ([\d.]+)"
    with open(file_path, 'r', errors='ignore') as file:
        throughput_sum = 0.0
        for line_num, line in enumerate(file, 1):
            # Search for the pattern in the line
            match = re.search(pattern, line)
            if match:
                client_number = match.group(1)
                throughput = match.group(2)
                throughput_sum += (float)(throughput)
                print(f"Line {line_num}: Client {client_number}, Throughput = {throughput}")
        print(f"Overall the throughput is: {throughput_sum}")
    return

def memcached_parser(file_path):
    print(file_path)
    pattern = r"\[OVERALL\], Throughput\(ops/sec\), ([\d.]+)"
    with open(file_path, 'r', errors='ignore') as file:
        for line_num, line in enumerate(file, 1):
            # Search for the pattern in the line
            match = re.search(pattern, line)
            if match:
                throughput = match.group(1)
                print(f"Line {line_num}: Throughput = {throughput}")
    return

def voltdb_parser(file_path):
    print(file_path)
    pattern = r"Transactions per second: ([\d.]+)"
    with open(file_path, 'r', errors='ignore') as file:
        for line_num, line in enumerate(file, 1):
            # Search for the pattern in the line
            match = re.search(pattern, line)
            if match:
                throughput = match.group(1)
                print(f"Line {line_num}: Throughput = {throughput}")
    return

def spark_parser(file_path):
    print("asd123www", file_path)
    results = []
    current_app = None

    def analyse_app(results):
        print(results[0][0])
        thro = [item[1] for item in results]
        thro.sort()
        print(thro)

    with open(file_path, 'r') as file:
        lines = file.readlines()

    for i, line in enumerate(lines):
        if "Throughput(bytes/s)" in line:
            data = re.split(r'\s+', lines[i + 1])
            throughput = data[5]
            current_app = data[0]
            if len(results) != 0 and results[-1][0] != current_app:
                analyse_app(results)
                results = []
            results.append((current_app, float(throughput)))
    analyse_app(results)

def parser(dir_path, apps):
    if not os.path.exists(dir_path):
        print(f"Directory {dir_path} does not exist.")
        return

    # List all files in the directory
    for filename in sorted(os.listdir(dir_path)):
        if filename.endswith(".txt"):
            file_path = os.path.join(dir_path, filename)

            print(filename)
            for app in apps:
                if app in file_path:
                    globals()[app + "_parser"](file_path)
            print("\n")

            # # Open and read the content of the file
            # with open(file_path, 'r') as file:
            #     content = file.read()
            #     print(f"Content of {filename}:")
            #     print(content)
            #     print("-" * 40)  # Separator for clarity



if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: python script.py <directory>")
        sys.exit(1)
    
    dir_path = sys.argv[1]
    print(f"Parsing {dir_path} experiment resuls")
    parser(dir_path, ["spark"])
    # parser(dir_path, ["redis", "memcached", "voltdb", "nginx", "gapbs", "spark"])
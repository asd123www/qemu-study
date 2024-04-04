import sys
import redis
import socket
import time
import random
import signal
import string
import multiprocessing
from multiprocessing import Process

PROXY_IPADDR = "10.10.1.100"
PROXY_PORT = 12345

KEY_LEN_MIN = 100
KEY_LEN_MAX = 101
VAL_LEN_MIN = 200
VAL_LEN_MAX = 201

WARMUP = 1000
REQNUM = 30000

CLIENT_NUM = 1
logging = []

def generate_random_string(length):
    letters = string.ascii_lowercase
    return ''.join(random.choice(letters) for i in range(length))

class testSelfDefinedClient():
    def __init__(self):
        # create a listening socket with a specific local interface and port
        self.s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.s.connect((PROXY_IPADDR, PROXY_PORT))
        
        self.send_str = b''
        self.recv_str = b''
        self.pending_str = b''
        
    def set(self, key, value):
        byte_stream = b'*3\r\n$3\r\nSET\r\n$' + str(len(key)).encode('utf-8') + b'\r\n' + key.encode('utf-8') + b'\r\n$' + str(len(value)).encode('utf-8') + b'\r\n' + value.encode('utf-8') + b'\r\n'
        self.pending_str += byte_stream
        self.send_str += b'+OK\r\n'
        
    def get(self, key, value):
        byte_stream = b'*2\r\n$3\r\nGET\r\n$' + str(len(key)).encode('utf-8') + b'\r\n' + key.encode('utf-8') + b'\r\n'
        self.pending_str += byte_stream
        self.send_str += b'$' + str(len(value)).encode('utf-8') + b'\r\n' + value.encode('utf-8') + b'\r\n'
    
    def flush_buffer(self):
        self.s.send(self.pending_str)
        self.pending_str = b''
        
        while True:
            tmp = self.s.recv(1024)
            self.recv_str += tmp
            if self.recv_str == self.send_str:
                # print(self.recv_str)
                # print(self.send_str)
                self.recv_str = b''
                self.send_str = b''
                break
                
    def run_test(self):
        begin_time = time.time()
        # start with closed-loop
        for i in range(10):
            # Get the value associated with a key
            key_len = random.randint(KEY_LEN_MIN, KEY_LEN_MAX)
            val_len = random.randint(VAL_LEN_MIN, VAL_LEN_MAX)
            
            key = generate_random_string(key_len)
            value = generate_random_string(val_len)
            self.set(key, value)
            self.flush_buffer()
            self.get(key, value)
            self.flush_buffer()
        
        for i in range(REQNUM):
            print(i)
            # Get the value associated with a key
            key_len = random.randint(KEY_LEN_MIN, KEY_LEN_MAX)
            val_len = random.randint(VAL_LEN_MIN, VAL_LEN_MAX)
            
            key = generate_random_string(key_len)
            value = generate_random_string(val_len)
            self.set(key, value)
            self.get(key, value)
            self.flush_buffer()
        
        print("Client-{0}: Throughput is {1}".format(client_id, 2 * (REQNUM + 10) / (time.time() - begin_time)))

class testRedisClient():
    def __init__(self):
        # create a Redis client object
        self.r = redis.Redis(host = PROXY_IPADDR, port = PROXY_PORT)
    
    def run_test(self):
        result = self.r.set("asd123www", "begin")
        if result != True:
            sys.exit("Error: the reply of SET is not OK.")
        
        begin_time = time.time()

        # Get the value associated with a key
        key_len = random.randint(KEY_LEN_MIN, KEY_LEN_MAX)
        val_len = random.randint(VAL_LEN_MIN, VAL_LEN_MAX)
        
        key = generate_random_string(key_len)
        value = generate_random_string(val_len)

        off_set = time.time_ns()
        for i in range(REQNUM):
            # set.
            start_time = time.time_ns()
            result = self.r.set(key, value)
            end_time = time.time_ns()
            if result != True:
                sys.exit("Error: the reply of SET is not OK.")
            if i > WARMUP:
                logging.append((start_time, end_time - start_time))

            # get, check the result.
            start_time = time.time_ns()
            result = self.r.get(key)
            end_time = time.time_ns()
            if result.decode('ascii') != value:
                sys.exit("Error: the reply of GET unequals to value.")
            if i > WARMUP:
                logging.append((start_time, end_time - start_time))
        
        print("Client-{0}: Throughput is {1}".format(client_id, 2 * REQNUM / (time.time() - begin_time)))

        with open('result.txt', 'w') as file:
            for t in logging:
                file.write(str((t[0] - off_set) / 1000000.0) + " " + str(t[1]) + "\n")

def run_client():
    if mode == 'closed-loop':
        testRedisClient().run_test()
    elif mode == 'open-loop':
        testSelfDefinedClient().run_test()
    
if __name__ == "__main__":
    n = len(sys.argv)
    if n < 3: 
        print("Total arguments passed:", n)
        sys.exit("Error: no mode or no client id.")
    
    mode = sys.argv[1]
    if mode != 'closed-loop' and mode != 'open-loop':
        sys.exit("Error: wrong mode.")
    client_id = sys.argv[2]
    
    print("Client-{1}: Start redis client with {0} mode".format(mode, client_id))
    
    run_client()
    
    print("Client-{0}: Finished sending requests!".format(client_id))
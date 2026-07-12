import paramiko
import sys

host = '192.168.164.140'
user = 'fy403'
pw = '123456'

c = paramiko.SSHClient()
c.set_missing_host_key_policy(paramiko.AutoAddPolicy())
c.connect(host, username=user, password=pw, timeout=10)

# Test 1: DNS resolution
_, o, _ = c.exec_command('nslookup 119.45.178.251 2>&1 || host 119.45.178.251 2>&1', timeout=5)
print("=== DNS ===")
print(o.read().decode())

# Test 2: Ping
_, o, _ = c.exec_command('ping -c 2 -W 3 119.45.178.251 2>&1', timeout=10)
print("=== PING ===")
print(o.read().decode())

# Test 3: Check if port 8000 is open
_, o, _ = c.exec_command('timeout 5 bash -c "echo > /dev/tcp/119.45.178.251/8000 && echo OPEN || echo CLOSED" 2>&1', timeout=10)
print("=== PORT 8000 ===")
print(o.read().decode())

# Test 4: Curl to HTTP
_, o, _ = c.exec_command('curl -v --connect-timeout 5 http://119.45.178.251:8000/ 2>&1 | head -20', timeout=10)
print("=== CURL ===")
print(o.read().decode())

# Test 5: Check if publisher can connect (look at its config)
_, o, _ = c.exec_command('cat /home/fy403/projects/recv/av_track/config.txt 2>&1', timeout=5)
print("=== CONFIG ===")
print(o.read().decode())

# Test 6: Check if signaling server is running locally
_, o, _ = c.exec_command('ss -tlnp 2>&1 | grep 8000 || netstat -tlnp 2>&1 | grep 8000', timeout=5)
print("=== LOCAL PORT 8000 ===")
print(o.read().decode())

c.close()

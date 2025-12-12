# 保存为 serial_read.py
# !/usr/bin/env python3
import serial
import threading
import time

# 使用 termios2 设置自定义波特率
ser = serial.Serial(
    port='/dev/ttyUSB1',
    baudrate=420000,  # PySerial 3.0+ 支持自定义波特率
    bytesize=8,
    parity='N',
    stopbits=1,
    timeout=1
)

# 要发送的十六进制数据
hex_data_to_send = "C8 18 16 DF CB 2B F8 C0 07 3E F0 81 0F 7C E0 03 1F F8 C0 07 3E F0 81 0F 7C 04"
bytes_to_send = bytes.fromhex(hex_data_to_send)

print(f"正在读取 /dev/ttyS5 @ {ser.baudrate} bps...")
print("按 Ctrl+C 停止")
print(f"准备发送数据: {hex_data_to_send}")


def send_data():
    """发送预定义的十六进制数据"""
    try:
        print(f"发送数据: {hex_data_to_send}")
        ser.write(bytes_to_send)
        print("数据发送完成")
    except Exception as e:
        print(f"发送数据时出错: {e}")


def receive_data():
    """接收并显示数据"""
    try:
        while True:
            if ser.in_waiting > 0:
                # 读取数据
                data = ser.read(ser.in_waiting)
                # 以十六进制格式显示数据
                hex_data = ' '.join(format(byte, '02x') for byte in data)
                print(f"接收数据: {hex_data.upper()}")
            time.sleep(0.01)  # 短暂休眠以减少CPU使用率
    except Exception as e:
        print(f"接收数据时出错: {e}")


# 创建并启动接收线程
receive_thread = threading.Thread(target=receive_data, daemon=True)
receive_thread.start()

# 发送数据
send_data()

try:
    # 等待用户中断
    while receive_thread.is_alive():
        time.sleep(0.1)
except KeyboardInterrupt:
    print("\n停止读取")
    ser.close()

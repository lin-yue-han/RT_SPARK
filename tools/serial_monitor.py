"""
串口监控脚本 - 自动读取 COM9 的 RT-Thread 终端输出
用法: python serial_monitor.py
按 Ctrl+C 退出
"""
import serial
import sys
import time

PORT = "COM9"
BAUD = 115200

def main():
    try:
        ser = serial.Serial(PORT, BAUD, timeout=1)
        print(f"[Monitor] Connected to {PORT} @ {BAUD}, waiting for data...")
        print("[Monitor] Press Ctrl+C to stop\n")
        
        while True:
            data = ser.readline()
            if data:
                try:
                    text = data.decode('utf-8', errors='replace').strip()
                    if text:
                        print(text)
                except:
                    print(f"[raw] {data}")
            else:
                # 没有数据时也发个心跳，确认连接还活着
                pass
                
    except serial.SerialException as e:
        print(f"[Error] Cannot open {PORT}: {e}")
        print("[Hint] Check if COM9 is available in Device Manager")
        print("[Hint] Make sure no other program (Putty, RT-Thread Studio) is using COM9")
        sys.exit(1)
    except KeyboardInterrupt:
        print("\n[Monitor] Stopped by user")
    finally:
        if 'ser' in locals():
            ser.close()

if __name__ == "__main__":
    main()

"""
串口监控脚本 v2 - 直接读原始字节，不按行等
"""
import serial
import sys
import time

PORT = "COM9"
BAUD = 115200

def main():
    try:
        ser = serial.Serial(PORT, BAUD, timeout=0.5)
        ser.reset_input_buffer()
        print(f"[Monitor] Connected to {PORT} @ {BAUD}")
        print("[Monitor] Reading raw bytes... (Ctrl+C to stop)\n")
        
        start = time.time()
        total = 0
        
        while True:
            # 直接读所有可用字节
            n = ser.in_waiting
            if n > 0:
                data = ser.read(n)
                total += len(data)
                try:
                    text = data.decode('utf-8', errors='replace')
                    print(text, end='', flush=True)
                except:
                    print(f"[raw {len(data)} bytes] {data.hex()}")
            else:
                time.sleep(0.1)
            
            # 每10秒报告一次状态
            elapsed = time.time() - start
            if elapsed > 10 and total == 0:
                print(f"\n[Monitor] 10s passed, 0 bytes received. Port may be wrong or board not sending.")
                start = time.time()
                
    except serial.SerialException as e:
        print(f"[Error] Cannot open {PORT}: {e}")
        sys.exit(1)
    except KeyboardInterrupt:
        print(f"\n[Monitor] Stopped. Total bytes: {total}")
    finally:
        if 'ser' in locals():
            ser.close()

if __name__ == "__main__":
    main()

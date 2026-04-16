import os
import serial.tools.list_ports

def find_board_port():
    ports = list(serial.tools.list_ports.comports())
    for p in ports:
        if "ACM" in p.device or "USB" in p.device or "STM32" in p.description:
            return p.device
    return "/dev/ttyACM0"

port = find_board_port()

print(f"Porta rilevata su {port}")
print("Avvio monitor seriale (Premi 'Ctrl + ]' per uscire)")

os.system(f"python3 -m serial.tools.miniterm {port} 115200")

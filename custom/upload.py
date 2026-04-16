import serial
import serial.tools.list_ports
import time
import sys

def find_board_port():
    ports = list(serial.tools.list_ports.comports())
    for p in ports:
        if "ACM" in p.device or "USB" in p.device or "STM32" in p.description:
            return p.device
    return "/dev/ttyACM0"

if len(sys.argv) == 2:
    filename = sys.argv[1]
    port = find_board_port()
    print(f"Porta rilevata automaticamente: {port}")
elif len(sys.argv) == 3:
    port = sys.argv[1]
    filename = sys.argv[2]
else:
    print("Uso: python3 upload.py [porta_opzionale] <file.wasm>")
    print("Esempio: python3 upload.py esempio.wasm")
    sys.exit(1)

try:
    with open(filename, "rb") as f:
        wasm_bytes = f.read()
        hex_data = wasm_bytes.hex().upper()
        print(f"File '{filename}' caricato ({len(wasm_bytes)} byte)")
except Exception as e:
    print(f"Errore nella lettura del file: {e}")
    sys.exit(1)

print(f"Connessione a {port}")

try:
    ser = serial.Serial(port, 115200, timeout=0.5)
    time.sleep(1)
    
    ser.reset_input_buffer()
    ser.reset_output_buffer()

    print("Invio in corso")
    ser.write(b"G")
    ser.flush()
    time.sleep(0.05)

    chunk_size = 16
    for i in range(0, len(hex_data), chunk_size):
        chunk = hex_data[i:i+chunk_size]
        ser.write(chunk.encode('utf-8'))
        ser.flush()
        time.sleep(0.01)

    time.sleep(0.05)
    ser.write(b"H")
    ser.flush()
    
    
    print("Trasferimento USB completato.")
    print("Il file è stato inviato alla scheda. Controlla il monitor seriale per verificare se Zephyr lo ha accettato o se gli slot sono pieni.")
    
    ser.close()

except Exception as e:
    print(f"Errore di comunicazione: {e}")

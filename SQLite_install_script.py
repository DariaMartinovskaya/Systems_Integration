import json
import sqlite3
from paho.mqtt import client as mqtt_client

mqtt_broker = '192.168.0.127'  
mqtt_port = 1883
mqtt_topic = 'esp32/sensors'
client_id = 'mqtt_sqlite_client'

db_file = 'sensor_data.db'

def init_db():
    conn = sqlite3.connect(db_file)
    cursor = conn.cursor()
    cursor.execute('''
        CREATE TABLE IF NOT EXISTS sensor_data (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            timestamp INTEGER,
            AcX REAL,
            AcY REAL,
            AcZ REAL,
            GyX REAL,
            GyY REAL,
            GyZ REAL,
            Temp REAL,
            Hum REAL,
            Btn INTEGER
        )
    ''')
    conn.commit()
    conn.close()

def insert_data(data):
    SELECT * FROM sensor_data ORDER BY timestamp DESC LIMIT 5;
    
    conn = sqlite3.connect(db_file)
    cursor = conn.cursor()
    cursor.execute('''
        INSERT INTO sensor_data (timestamp, AcX, AcY, AcZ, GyX, GyY, GyZ, Temp, Hum, Btn)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    ''', (
        data.get('timestamp'),
        data.get('AcX'),
        data.get('AcY'),
        data.get('AcZ'),
        data.get('GyX'),
        data.get('GyY'),
        data.get('GyZ'),
        data.get('Temp'),
        data.get('Hum'),
        data.get('Btn')
    ))
    conn.commit()
    conn.close()

def on_connect(client, userdata, flags, rc):
    if rc == 0:
        print("Connected to MQTT Broker!")
        client.subscribe(mqtt_topic)
    else:
        print(f"Failed to connect, return code {rc}")

def on_message(client, userdata, msg):
    print(f"Received {msg.payload.decode()} from {msg.topic} topic")
    try:
        data = json.loads(msg.payload.decode())
        insert_data(data)
        print("Data inserted into SQLite")
    except Exception as e:
        print("Error:", e)

def run():
    init_db()
    client = mqtt_client.Client(client_id)
    client.on_connect = on_connect
    client.on_message = on_message
    client.connect(mqtt_broker, mqtt_port)
    client.loop_forever()

if name == '__main__':
    run()

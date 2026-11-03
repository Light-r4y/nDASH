Display devboard: GUITION JC8048W550C

CAN tranceiver: TJA1050

Power module: MP1584

Screw : DIN 912 M3x16

Heat brass insert nut: M3x5x5

-------------------------------------------------------------------

Connectors:

Adapter -- ECU wiring (JST XH 2.54 4pin)

GND / 12V / CAN_L / CAN_H

-------------------------------------------------------------------

P1 Power

(use 4pin JST1.25 connector near the USB)

Power module MP1584 Vout -- 5V

RX/TX unused

Power module MP1584 GND -- GND

## CANbus

P4 CANbus

Adapter -- Display devboard (4pin JST1.25)

CAN transceiver VCC -- ~~3.3V~~ !!! Note: in latest firmware(bench, start/stop, ...) not working, use 5V form another place

CAN transceiver GND -- GND

CAN transceiver TX -- PIN_17

CAN transceiver RX -- PIN_18 (buld resistor divider to 3.3V input or use 3V CAN transceiver)


![assembled1](https://github.com/Light-r4y/dash5_esp32s3/blob/v1/media/7.png)


![assembled2](https://github.com/Light-r4y/dash5_esp32s3/blob/v1/media/8.png)


![assembled3](https://github.com/Light-r4y/dash5_esp32s3/blob/v1/media/6.jpeg)

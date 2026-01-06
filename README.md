### **2D MINECRAFT on ESP32S3**

This project is for ESP32-S3 with OTFTSPI GMT020-02-8P display (240x320px) and it is a 2D minecraft where you can move, dig,
build and much more. So far there is simple kicking, jumping, walking and world and tree generation including simple materials.
_(There are still some bugs, but they are being worked on. I would be happy for support either verbal or advice / bugfix)_

### **Components + Wiring**

**DISPLAY:** https://docs.cirkitdesigner.com/component/c0003012-c9ff-40db-885f-f1759f5a2cdc/20-inch-tft-lcd-display
**BL**-3,3V,  **CS**-13,  **DC**-9,  **RST**-10,  **SDA**-11,  **SCL**-12,  **VCC**-3,3V, **GND**-GND

**2x2 BUTTONS:** https://gleanntronics.ie/en/products/2x2-tact-switch-keyboard-matrix-of-4-buttons-for-arduino-1605.html
**L1**-6,  **L2**-7,  **R1**-4,  **R2**-3

**ESP32S3:**  https://www.ebay.de/itm/376447694668?chn=ps&_ul=DE&var=645072555269&google_free_listing_action=view_item

**BUTTON_1:** https://forum.arduino.cc/t/how-to-connect-these/667059
**VCC**-5V,  **GND**-GND, **OUT**-17    _(Since ESP32-S3 does not have 5V, I use Arduino nano as a 5V power source)_

**BUTTON_2:** https://forum.arduino.cc/t/how-to-connect-these/667059
**VCC**-5V,  **GND**-GND, **OUT**-87    _(Since ESP32-S3 does not have 5V, I use Arduino nano as a 5V power source)_

**+BONUS:**  https://dev.blues.io/blog/blues-university-first-components-breadboard/
         https://www.joom.com/sk/products/6038c565f242f00107e8b2a6?openPayload=%7B%22position%22%3A3%7D

### **Controlling**
- Go left - **S1**
- Go right - **S2**
- Block selection - **S3**
- Block mining - **S4**
- Jump - **17**
- Debug Screen - **18**

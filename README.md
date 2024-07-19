I developed a system on a Raspberry PI 4 to translate text into morse code, and relay it to an ESP32C3 RUST microcontroller via a flashing LED. The ESP board is constantly reading a data stream from a photodiode positioned in front of the LED transmitter.  
The maximum observed transfer rate approached ~10 char/sec before artifacting occured.   

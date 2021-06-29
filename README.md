# BoilerTemp
Solar water heating switching system.

## Background
Some residential buildings have a central solar water heating system. Water (hereafter: shared water) is heated in solar panels on the roof, and than pushed through the boiler in each apartment. Note that this shared water is not mixed with the private water in each boiler; but just passes through a heat exchanger. Heat then transfers from the hotter water to the colder water.

Residents may decide to opt out of the system by closing a valve near the boiler.

## Motivation
Ideally, heat will pass from the shared water to the private water of each boiler, and not the other way around. Unfortunately, there is no mechanism in place to ensure that. When the sun no longer heats the solar panels, the shared water will cool, yet the private waters will still be hot from being heated during the day. Forcing the shared water through the boilers will effectively cool them.
In addition, a resident may heat the water electrically. Nothing prevents the shared water from stealing the heat from the electrically heated water. This effectively reduces the effectivenes of the electrical heater, increasing electrical bills and reducing user satisfaction.

## The Solution
This project consists of:
* An ESP8266 microcontroller.
* 3 temperature sensors.
* A4988 stepper driver, a stepper motor, a belt, and 3D printed valve handle adapter.

The sensors are located:
1. On the shared water pipe.
2. On the shared water intake pipe, as close as possible to the boiler.
3. On the thermostat, inserted in the bottom of the boiler.

The MCU reads the temperatures, logs them, and decides whether to open or close the valve.
It also outputs a nice graph of past temperatures and current valve status, accessible through the LAN.

### Valve Opening and closing logic
The numbers in this section refer to the sensor location defined above.

Let `lowerTempCutoff` be 8 degrees below the maximum temperature measured at 2.

The valve should close if any of these conditions is met:
* Temp 2 > Temp 1
* Temp 3 > Temp 1
* Temp 3 > Temp 2
* Temp 1 > `lowerTempCutoff`

The valve should open if this condition is met:
* Temp 1 > (Temp 2) + 1

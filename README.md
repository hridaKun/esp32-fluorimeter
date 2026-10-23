# ESP32 Handheld Fluorimeter

## Folder structure

    Schematic/      Circuit.kicad_sch (KiCad), Circuit.pdf
    Firmware/       fluorimeter/fluorimeter.ino (open in Arduino IDE)
    Simulation/
      LTspice_models/ 1_dc_transfer.cir, 2_led_pulse.cir, 3_stability_noCf.cir
      Outputs/        plots (.png), data (.csv), results_summary.txt

## How it works

Blue LED excites the sample. The BPW34 photodiode (at 90 deg, behind a
long-pass filter) turns the fluorescence into a tiny current. The MCP602
transimpedance amplifier turns that current into a voltage (1 mV per nA).
The ADS1115 digitises it and the ESP32 shows the result on the TFT.

Each reading switches the LED off and on 32 times and averages
(LED on - LED off), which removes room light and offsets.

## Screen

    FLUORIMETER        BLANK: OK / --    SINGLE / CONTINUOUS
                       CAL:   OK / --
    CONCENTRATION      (or SIGNAL before calibration)
    1.234 uM           big number
    Net:   12.345 nA   fluorescence current (LED on - LED off)
    Noise: +/-0.012    uncertainty of the reading
    Dark:  1.234 nA    background current with the LED off
    message line       Press MEASURE / Blank stored / SATURATED ...

## Use

Buttons: MEASURE (press = one reading, hold 1 s = continuous on/off),
BLANK (press = store blank).

Serial monitor (115200): m, blank, std <conc>, fit, clear

Calibration: plain solvent -> BLANK, each standard -> std 1, std 2 ...,
then fit. After that the screen shows concentration.

## Libraries

Adafruit ADS1X15, Adafruit ILI9341, Adafruit GFX. Board: ESP32 Dev Module.

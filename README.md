# i401
Add functionality for time signal with Kanmusu Nendoroid. Read and play sound in microSD card using PIC16F1705. 

## Setup
### Hardware
#### Circuit
![screenshot](https://raw.githubusercontent.com/manse/i401/master/readme/i401.png)
- Connect Pickit with `JP1` 

#### SD Card
1. Format SD card as FAT16
1. Make directory named `KC-***` (e.g. `KC-i401`, `KC-suzuya`)
2. Obtain Kanmusu's time signal sounds from the Internet
3. Convert sounds to `WAV`, `22050 Hz`, `1 channel`, `8 bit depth`
4. Put WAVs into the directory as `00.wav` ~ `23.wav`

### Software
1. Import project into MPLABX IDE
2. Select PIC16F1705 //TODO

## Usage
1. On the hour, turn on device
2. Select current hour using `S2` 
3. Select Kanmusu using `S3` (switch the `KC-***` directory)
4. Put device into a head of Nendoroid

Then automatically plays WAV every 1 hour.

MIT

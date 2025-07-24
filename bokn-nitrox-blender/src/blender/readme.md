

## Wiring

### Components
#### Relay
The relay controls: 
- Start contactor
- Stop contactor
- Open solenoid

## Logic

var peripheralsStatus {
    solenoidOpened: true/false
    compressorStarted: true/false
    
}


loop() {

    Read O2 voltage
    Read Pressure

    if (autostop is enabled && pressure > 250) {
        trigger stop (same as stop button)
    }
}

function stopCompressor() {
    setCompressorStatus("stopped");
    closeSolenoid();
}

function startCompressor() {
    setCompressorStatus("started");
    openSolenoid();
}




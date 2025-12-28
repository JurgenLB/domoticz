# Debugging RFXcom Messages for Orcon Devices

This document explains the `sDecodeRXMessage` mechanism in Domoticz and how to debug messages from Orcon ventilation devices received via RFXcom transceivers.

## Table of Contents
1. [Overview](#overview)
2. [Message Flow](#message-flow)
3. [Understanding sDecodeRXMessage](#understanding-sdecodeRXmessage)
4. [Orcon Message Structure](#orcon-message-structure)
5. [Enabling Debug Output](#enabling-debug-output)
6. [Decoding Example Messages](#decoding-example-messages)
7. [Troubleshooting](#troubleshooting)

## Overview

`sDecodeRXMessage` is a **Boost.Signals2** signal that serves as the primary mechanism for hardware devices to communicate received messages to the Domoticz main worker thread for processing.

### Key Components
- **Signal Definition**: `hardware/DomoticzHardware.h`
- **Signal Connection**: `main/mainworker.cpp` (in `AddDomoticzHardware()`)
- **Message Processing**: `main/mainworker.cpp` (in `DecodeRXMessage()`, `ProcessRXMessage()`)
- **Orcon Decoding**: `main/mainworker.cpp` (in `decode_Fan()`)
- **Protocol Structures**: `main/RFXtrx.h`

## Message Flow

The message flow from hardware to database follows this path:

```
RFXcom Hardware Receives RF Signal
         ↓
    RFXBase::ParseData()
         ↓
    sDecodeRXMessage.emit()  [Signal emission]
         ↓
    MainWorker::DecodeRXMessage()  [Signal handler]
         ↓
    MainWorker::PushRxMessage()
         ↓
    [Queue System]
         ↓
    MainWorker::ProcessRXMessage()
         ↓
    MainWorker::decode_Fan()  [For pTypeFan messages]
         ↓
    SQL Database Update
```

### Signal-Slot Mechanism

When a hardware device is added to Domoticz:

```cpp
// hardware/DomoticzHardware.h
boost::signals2::signal<void(CDomoticzHardwareBase *pHardware, 
                              const unsigned char *pRXCommand, 
                              const char *defaultName, 
                              const int BatteryLevel, 
                              const char *userName)> sDecodeRXMessage;
```

The main worker connects to this signal:

```cpp
// main/mainworker.cpp - AddDomoticzHardware()
pHardware->sDecodeRXMessage.connect([this](auto hw, auto rx, auto name, auto battery, auto userName) { 
    DecodeRXMessage(hw, rx, name, battery, userName); 
});
```

## Understanding sDecodeRXMessage

### Signal Parameters

1. **pHardware**: Pointer to the hardware device that received the message
2. **pRXCommand**: Raw byte array containing the RFXcom message
3. **defaultName**: Optional default device name (can be `nullptr`)
4. **BatteryLevel**: Battery level indicator (-1 if not applicable)
5. **userName**: User who triggered the message (for manual commands)

### Message Structure

All RFXcom messages follow this basic format:

```
Byte 0: Packet Length (number of bytes following this byte)
Byte 1: Packet Type (e.g., 0x17 for pTypeFan)
Byte 2: Subtype (e.g., 0x0C for sTypeOrcon)
Byte 3: Sequence Number
...remaining bytes vary by packet type...
```

## Orcon Message Structure

### Basic FAN Structure

For standard Orcon commands (packet length 0x08):

```cpp
struct FAN {
    BYTE packetlength;  // 0x08 (8 bytes following)
    BYTE packettype;    // 0x17 (pTypeFan)
    BYTE subtype;       // 0x0C (sTypeOrcon)
    BYTE seqnbr;        // Sequence number (0-255)
    BYTE id1;           // Device ID byte 1
    BYTE id2;           // Device ID byte 2
    BYTE id3;           // Device ID byte 3
    BYTE cmnd;          // Command code
    BYTE rssi:4;        // Signal strength (0-15)
    BYTE filler:4;      // Padding bits
};
```

### Extended Orcon Structure (FANEXT)

For advanced Orcon status messages (packet length 0x31):

```cpp
struct FANEXT {
    BYTE packetlength;  // 0x31 (49 bytes following)
    BYTE packettype;    // 0x17 (pTypeFan)
    BYTE subtype;       // 0x0C (sTypeOrcon)
    BYTE seqnbr;        // Sequence number
    BYTE id1;           // Device ID byte 1
    BYTE id2;           // Device ID byte 2
    BYTE id3;           // Device ID byte 3
    BYTE cmnd;          // Command code
    BYTE rssi:4;        // Signal strength
    BYTE filler:4;      // Padding bits
    BYTE did1;          // Destination ID byte 1
    BYTE did2;          // Destination ID byte 2
    BYTE did3;          // Destination ID byte 3
    BYTE ext1-ext31;    // Extended data (31 bytes)
};
```

### Orcon Commands

The following commands are supported for Orcon devices:

| Command Byte | Constant Name       | Value | Description                    |
|--------------|---------------------|-------|--------------------------------|
| 0x01         | fan_Orconlow        | 1     | Low speed                      |
| 0x02         | fan_Orconmedium     | 2     | Medium speed                   |
| 0x03         | fan_Orconhigh       | 3     | High speed                     |
| 0x04         | fan_Orcontimer1     | 4     | Timer mode 1                   |
| 0x05         | fan_Orcontimer2     | 5     | Timer mode 2                   |
| 0x06         | fan_Orcontimer3     | 6     | Timer mode 3                   |
| 0x07         | fan_Orconauto       | 7     | Automatic mode                 |
| 0x08         | fan_Orconaway       | 8     | Away mode                      |
| 0x09         | fan_Orconjoin       | 9     | Join network                   |
| 0x0A         | fan_Orconleave      | 10    | Leave network                  |
| 0x0B         | fan_Orconstate      | 11    | State report                   |
| 0x0C         | fan_Orcontemp       | 12    | Temperature sensor data        |
| 0x0D         | fan_Orconco2        | 13    | CO2 sensor data                |
| 0x0E         | fan_Orconbattery    | 14    | Battery status                 |
| 0x0F         | fan_Orconfilter     | 15    | Filter status                  |
| 0x10         | fan_Orconpresence   | 16    | Presence detection             |
| 0x11         | fan_Orconspeed      | 17    | Speed report                   |
| 0x12         | fan_Orconstatus     | 18    | General status report          |

### Device ID Formatting

In Domoticz, the device ID is created by concatenating the three ID bytes from the RFXcom message into a hexadecimal string. This is done in the `decode_Fan()` function:

```cpp
sprintf(szTmp, "%02X%02X%02X", pResponse->FAN.id1, pResponse->FAN.id2, pResponse->FAN.id3);
std::string ID = szTmp;
```

**Explanation**:
- **`sprintf()`**: C function that formats data and writes it to a string buffer
- **`szTmp`**: Character array buffer to store the formatted string
- **`"%02X%02X%02X"`**: Format string with three format specifiers:
  - `%02X`: Format each byte as a 2-digit hexadecimal number (uppercase)
    - `0`: Pad with leading zeros if needed
    - `2`: Always output exactly 2 characters
    - `X`: Use uppercase hexadecimal (A-F)
- **`pResponse->FAN.id1, id2, id3`**: The three ID bytes from the message

**Example**:
```
If the message contains:
  id1 = 0x12
  id2 = 0x34
  id3 = 0x56

The sprintf produces: "123456"

This becomes the unique device identifier in Domoticz's database.
```

**Why This Matters**:
- Each Orcon device has a unique 3-byte (24-bit) ID
- Domoticz uses this ID string to identify and track specific devices
- Multiple devices with different IDs can coexist in the same system
- The ID is displayed in debug logs to help identify which device sent a message

**Common ID Values**:
- New/unpaired devices may have ID `000000`
- Factory-paired devices typically have random IDs like `A1B2C3`
- Remote controls and sensors each have unique IDs

## Enabling Debug Output

### Method 1: Set Debug Level

To enable detailed RFXcom message debugging:

1. **Via Web UI**: 
   - Go to Setup → Settings → System
   - Enable "Debug (receive)" option
   - Save settings

2. **Via Application Arguments**:
   ```bash
   ./domoticz -debuglevel 1
   ```

3. **Via Environment Variable**:
   ```bash
   export DOMOTICZ_DEBUG=RECEIVED
   ```

### Method 2: Enable Log Level at Compile Time

In `main/Logger.cpp` or via runtime configuration, ensure `DEBUG_RECEIVED` is enabled.

### Method 3: Direct Test Mode

For development and testing, you can inject test messages:

```cpp
// In main/mainworker.cpp - Debug code example
unsigned char test_message[] = {
    0x08,        // Length
    0x17,        // pTypeFan
    0x0C,        // sTypeOrcon
    0x01,        // Sequence number
    0x12, 0x34, 0x56,  // Device ID
    0x02,        // Command (fan_Orconmedium)
    0x80         // RSSI + filler
};

pHardware->sDecodeRXMessage(pHardware, test_message, nullptr, -1, nullptr);
```

## Decoding Example Messages

### Example 1: Basic Orcon Medium Speed Command

**Raw Message (Hex)**: `08 17 0C 01 12 34 56 02 80`

**Breakdown**:
```
Byte 0 (0x08): Packet length = 8 bytes following
Byte 1 (0x17): Packet type = pTypeFan (0x17)
Byte 2 (0x0C): Subtype = sTypeOrcon (0x0C)
Byte 3 (0x01): Sequence number = 1
Byte 4-6 (0x12 0x34 0x56): Device ID = 0x123456
Byte 7 (0x02): Command = fan_Orconmedium
Byte 8 (0x80): RSSI = 8 (signal level 8/15), filler = 0
```

**Expected Debug Output**:
```
subtype       = Orcon
Sequence nbr  = 1
ID            = 123456
Command       = Medium
Signal level  = 8
```

### Example 2: Orcon High Speed with Strong Signal

**Raw Message (Hex)**: `08 17 0C 05 AB CD EF 03 A0`

**Breakdown**:
```
Byte 0 (0x08): Length = 8
Byte 1 (0x17): Type = pTypeFan
Byte 2 (0x0C): Subtype = sTypeOrcon
Byte 3 (0x05): Sequence = 5
Byte 4-6 (0xAB 0xCD 0xEF): Device ID = 0xABCDEF
Byte 7 (0x03): Command = fan_Orconhigh (high speed)
Byte 8 (0xA0): RSSI = 10 (strong signal)
```

### Example 3: Extended Orcon Status Message

**Raw Message (Hex)**: `31 17 0C 02 12 34 56 12 80 00 00 00 [28 more ext bytes]...`

**Breakdown**:
```
Byte 0 (0x31): Length = 49 bytes (extended message)
Byte 1 (0x17): Type = pTypeFan
Byte 2 (0x0C): Subtype = sTypeOrcon
Byte 3 (0x02): Sequence = 2
Byte 4-6 (0x12 0x34 0x56): Source Device ID
Byte 7 (0x12): Command = fan_Orconstatus (status report)
Byte 8 (0x80): RSSI = 8
Byte 9-11: Destination device ID (if applicable)
Byte 12-49: Extended status data (temperature, CO2, etc.)
```

## Troubleshooting

### Problem: No Debug Output Visible

**Solutions**:
1. Verify debug level is set correctly:
   ```cpp
   if (_log.IsDebugLevelEnabled(DEBUG_RECEIVED))
   ```
2. Check that the RFXcom transceiver is properly configured
3. Ensure the Orcon device is in range and transmitting
4. Verify the hardware is enabled in Domoticz

### Problem: Message Received but Not Decoded

**Check**:
1. Packet type is 0x17 (pTypeFan)
2. Subtype is 0x0C (sTypeOrcon)
3. Packet length is correct (0x08, 0x11, or 0x31)
4. The message passes `CheckValidRFXData()` validation in `RFXBase.cpp`:
   ```cpp
   case pTypeFan:
       return (pLen == 0x08 || pLen == 0x11 || pLen == 0x31);
   ```

### Problem: Unknown Command Code

If you see an unknown command in the logs:

1. Check `main/RFXtrx.h` for the command definition
2. Verify your RFXcom firmware version supports the command
3. The command might be a vendor-specific extension

### Problem: Signal Strength Always Low

**Check**:
1. RSSI byte format (lower 4 bits on little-endian systems)
2. Distance between Orcon device and RFXcom transceiver
3. RF interference from other devices
4. Antenna connection on RFXcom device

### Validation Checks

The message must pass these validation checks (from `RFXBase.cpp`):

```cpp
bool CRFXBase::CheckValidRFXData(const uint8_t *pData)
{
    uint8_t pLen = pData[0];
    uint8_t pType = pData[1];
    
    // For Fan messages (including Orcon with extended FANEXT support)
    case pTypeFan:
        return (pLen == 0x08 || pLen == 0x11 || pLen == 0x31);
}
```

### Inspecting Raw Messages

To see the raw hexadecimal message bytes, add logging in `RFXBase.cpp`:

```cpp
_log.Debug(DEBUG_HARDWARE, "CheckValidRFXData: pLen=0x%02X, pType=0x%02X (%s), pData=%s", 
           pLen, pType, RFX_Type_Desc(pType, 1), ToHexString(pData, pLen + 1).c_str());
```

This will output messages like:
```
CheckValidRFXData: pLen=0x08, pType=0x17 (FAN), pData=08170C0112345602180
```

## Code Walkthrough: decode_Fan() for Orcon

Understanding how the `decode_Fan()` function processes Orcon messages can help with debugging. Here's a step-by-step walkthrough:

### Step 1: Extract Device Information

```cpp
void MainWorker::decode_Fan(const CDomoticzHardwareBase* pHardware, const tRBUF* pResponse, _tRxMessageProcessingResult& procResult)
{
    char szTmp[100];
    uint8_t devType = pTypeFan;                    // Device type = 0x17
    uint8_t subType = pResponse->FAN.subtype;      // Get subtype (0x0C for Orcon)
    
    // Format the 3-byte device ID as a hexadecimal string
    sprintf(szTmp, "%02X%02X%02X", pResponse->FAN.id1, pResponse->FAN.id2, pResponse->FAN.id3);
    std::string ID = szTmp;                        // e.g., "123456"
    
    uint8_t Unit = 0;                              // Unit number (0 for fans)
    uint8_t cmnd = pResponse->FAN.cmnd;            // Command byte
    uint8_t SignalLevel = pResponse->FAN.rssi;     // Signal strength (0-15)
```

**Key Points**:
- `pResponse->FAN` is a structure pointer to the received message
- The ID is formatted using `sprintf()` with `%02X` format specifiers
- Each ID byte becomes 2 hexadecimal digits in the resulting string
- Example: bytes `0x12 0x34 0x56` become string `"123456"`

### Step 2: Update Database

```cpp
    uint64_t DevRowIdx = m_sql.UpdateValue(
        pHardware->m_HwdID,    // Hardware ID
        0,                      // Port number
        ID.c_str(),            // Device ID string
        Unit,                   // Unit number
        devType,               // Device type (pTypeFan)
        subType,               // Subtype (sTypeOrcon)
        SignalLevel,           // RSSI value
        -1,                    // Battery level (-1 = N/A)
        cmnd,                  // Command code
        procResult.DeviceName, // Device name
        true,                  // Allow auto-creation
        procResult.Username.c_str()  // User who triggered
    );
```

**Key Points**:
- `UpdateValue()` either updates an existing device or creates a new one
- The ID string uniquely identifies the device in the database
- Returns the database row index for the device

### Step 3: Debug Output (when enabled)

```cpp
    if (_log.IsDebugLevelEnabled(DEBUG_RECEIVED))
    {
        WriteMessageStart();
        switch (pResponse->FAN.subtype)
        {
        case sTypeOrcon:
            WriteMessage("subtype       = Orcon");
            sprintf(szTmp, "Sequence nbr  = %d", pResponse->FAN.seqnbr);
            WriteMessage(szTmp);
            sprintf(szTmp, "ID            = %02X%02X%02X", pResponse->FAN.id1, pResponse->FAN.id2, pResponse->FAN.id3);
            WriteMessage(szTmp);
            WriteMessage("Command       = ", false);  // false = no line feed
            
            switch (pResponse->FAN.cmnd)
            {
            case fan_Orconlow:
                WriteMessage("Low");
                break;
            case fan_Orconmedium:
                WriteMessage("Medium");
                break;
            // ... additional command cases ...
            }
            break;
        }
        sprintf(szTmp, "Signal level  = %d", pResponse->FAN.rssi);
        WriteMessage(szTmp);
        WriteMessageEnd();
    }
```

**Key Points**:
- Debug output only appears when `DEBUG_RECEIVED` level is enabled
- `WriteMessage()` functions log to the Domoticz log system
- Command bytes are decoded to human-readable strings
- Signal level (RSSI) ranges from 0 (weak) to 15 (strong)

### Complete Flow Summary

```
1. RFXcom receives RF signal from Orcon device
2. RFXBase::ParseData() extracts message bytes
3. sDecodeRXMessage signal emits the message
4. MainWorker::DecodeRXMessage() receives it
5. Message queued for processing
6. MainWorker::ProcessRXMessage() dispatches by type
7. MainWorker::decode_Fan() called for pTypeFan
8. Device ID extracted and formatted as hex string
9. Database updated with device state
10. Debug output generated (if enabled)
11. Scene/event checks performed
```

## Additional Resources

- **RFXcom Protocol Documentation**: Included in RFXtrx SDK
- **Domoticz Wiki**: https://wiki.domoticz.com/
- **Domoticz Forum**: https://forum.domoticz.com/
- **RFXtrx.h**: Complete protocol definitions in `main/RFXtrx.h`
- **RFXNames.cpp**: Human-readable device type names in `main/RFXNames.cpp`

## Code References

### Key Files
- `hardware/DomoticzHardware.h`: Signal definition
- `hardware/RFXBase.cpp`: Message reception and validation
- `main/mainworker.cpp`: Message processing and decoding
- `main/RFXtrx.h`: Protocol structures and constants
- `main/RFXNames.cpp`: Device type name mappings

### Key Functions
- `sDecodeRXMessage`: Signal for message emission
- `RFXBase::ParseData()`: Parses received RF data
- `MainWorker::DecodeRXMessage()`: Initial message handler
- `MainWorker::ProcessRXMessage()`: Message type dispatcher
- `MainWorker::decode_Fan()`: Fan-specific decoder (includes Orcon)
- `RFXBase::CheckValidRFXData()`: Message validation

## Contributing

If you discover new Orcon commands or improved debugging techniques, please contribute to this documentation by submitting a pull request to the Domoticz repository.

---

**Document Version**: 1.0  
**Last Updated**: 2024  
**Domoticz SDK Version**: 9.44  
**Orcon Support Added**: SDK 9.34 (November 2022)  
**Extended Orcon Structure**: SDK 9.39 (February 2023)

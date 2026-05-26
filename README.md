# GD32 BLE LED Terminal Example

This project is a BLE peripheral example for the GD32VW55x platform. It exposes a BLE data transfer service that allows an Android BLE terminal application to control three GPIO LEDs using simple text commands.

The board advertises itself as:

```text
GD32-BLE-LED
```

After connecting from a BLE terminal application, the phone can write commands such as `L1`, `D1`, `ON`, `OFF`, `STATUS`, or `HELP`. The board updates the LEDs and sends a text response back over BLE notification.

---

## Project Purpose

The goal of this example is to demonstrate a simple BLE UART-like interface using the GD32 BLE Data Transfer Service.

It is suitable for hands-on training because it shows:

- BLE advertising setup
- BLE peripheral connection handling
- BLE data reception callback
- BLE notification transmission
- GPIO output control
- Command parsing from a mobile application
- Basic embedded application structure using the GD32 SDK and RTOS wrapper

---

## Hardware Used

The example controls three LEDs connected to the following GPIOs:

| LED | GPIO Port | GPIO Pin |
|-----|-----------|----------|
| LED1 | GPIOB | PB0 |
| LED2 | GPIOA | PA12 |
| LED3 | GPIOB | PB4 |

The LED polarity is configured by this macro:

```c
#define LED_ACTIVE_LOW  0
```

Meaning:

| Value | Behavior |
|-------|----------|
| `0` | LED turns on when the GPIO is driven high |
| `1` | LED turns on when the GPIO is driven low |

Change this macro only if your hardware uses active-low LEDs.

---

## BLE Device Information

| Item | Value |
|------|-------|
| BLE role | Peripheral |
| Advertising name | `GD32-BLE-LED` |
| BLE service | GD32 BLE Data Transfer Service |
| Phone role | Central/client |
| Communication style | BLE write from phone, BLE notification from board |

The application starts BLE advertising automatically after initialization. When a phone connects, the board stores the connection index and uses it to send responses back to the BLE terminal.

---

## Supported Commands

Commands are sent as ASCII text from the mobile BLE terminal.

| Command | Action | Response |
|---------|--------|----------|
| `L1` | Turn LED1 on | `led1 ligado` |
| `L2` | Turn LED2 on | `led2 ligado` |
| `L3` | Turn LED3 on | `led3 ligado` |
| `D1` | Turn LED1 off | `led1 desligado` |
| `D2` | Turn LED2 off | `led2 desligado` |
| `D3` | Turn LED3 off | `led3 desligado` |
| `T1` | Toggle LED1 | Current LED1 state |
| `T2` | Toggle LED2 | Current LED2 state |
| `T3` | Toggle LED3 | Current LED3 state |
| `ON` | Turn all LEDs on | `todos ligados` |
| `OFF` | Turn all LEDs off | `todos desligados` |
| `STATUS` | Read all LED states | Current LED states |
| `HELP` | Show command list | Available commands |
| `?` | Show command list | Available commands |

The command parser removes spaces, tabs, carriage returns, and line feeds. It also converts commands to uppercase.

Therefore, all of the following are accepted as the same command:

```text
L1
l1
L1\r\n
 l1
```

---

## Application Flow

The main application flow is:

```text
System initialization
        ↓
Platform initialization
        ↓
LED GPIO initialization
        ↓
BLE stack initialization
        ↓
BLE advertising starts
        ↓
Phone connects
        ↓
Phone writes command through BLE
        ↓
BLE RX callback receives data
        ↓
Command is normalized and processed
        ↓
LED GPIO state is updated
        ↓
Board sends response by BLE notification
```

---

## Main Source File

The main application logic is implemented in:

```text
main.c
```

Important sections in the file:

| Section | Description |
|---------|-------------|
| Device name | Defines the BLE advertising name |
| LED configuration | Defines GPIO mapping and LED polarity |
| LED helpers | Initializes and updates GPIO LEDs |
| BLE terminal helpers | Sends text responses through BLE |
| Command parser | Processes commands received from the phone |
| Advertising management | Creates and starts BLE advertising |
| Connection handling | Tracks BLE connection and disconnection events |
| Security callbacks | Handles BLE pairing and security events |
| BLE initialization | Configures and starts the BLE stack |
| Main function | Starts the system, platform, LEDs, BLE, and RTOS scheduler |

---

## Important Functions

### `leds_init()`

Initializes the GPIO clocks and configures PB0, PA12, and PB4 as digital outputs. It also sets the initial LED state to off.

### `leds_apply()`

Writes the logical LED states to the physical GPIO pins.

### `led_write()`

Converts a logical LED state into the correct GPIO level, considering the `LED_ACTIVE_LOW` configuration.

### `normalize_command()`

Prepares the received command before comparison by:

- Removing spaces and line endings
- Removing tab characters
- Converting letters to uppercase
- Keeping only a clean null-terminated C string

### `process_ble_command()`

Compares the normalized command with the supported command list, updates the LED states, and sends a response to the phone.

### `app_datatrans_srv_rx_callback()`

Receives BLE data written by the phone. It copies the received bytes into a local command buffer and forwards the command to `process_ble_command()`.

### `ble_terminal_send()`

Sends text responses to the connected phone using BLE notification.

### `app_adv_create()` and `app_adv_start()`

Create and start the BLE advertising set. These functions make the board visible as `GD32-BLE-LED`.

### `app_conn_evt_handler()`

Tracks BLE connection state. It updates the connection index when a phone connects and clears it when the phone disconnects.

### `ble_init()`

Initializes the BLE stack, configures the peripheral role, registers callbacks, initializes the data transfer service, and enables BLE interrupts.

---

## How to Test

1. Build and flash the firmware to the GD32VW55x board.
2. Open a BLE terminal application on Android.
3. Scan for BLE devices.
4. Connect to the device named `GD32-BLE-LED`.
5. Open the writable characteristic from the data transfer service.
6. Send one of the supported commands, for example:

```text
L1
```

7. LED1 should turn on and the terminal should receive:

```text
led1 ligado
```

8. Send:

```text
STATUS
```

9. The board should return the current LED states.

---

## Example BLE Terminal Session

```text
> HELP
Commands: L1 L2 L3 D1 D2 D3 T1 T2 T3 ON OFF STATUS HELP

> L1
led1 ligado

> L2
led2 ligado

> STATUS
LED1=ON LED2=ON LED3=OFF

> T1
led1 desligado

> OFF
todos desligados
```

---

## Configuration Points

### Change BLE Device Name

Edit:

```c
#define DEV_NAME "GD32-BLE-LED"
```

### Change LED GPIO Pins

Edit the LED mapping macros:

```c
#define LED1_PORT       GPIOB
#define LED1_PIN        GPIO_PIN_0
#define LED1_RCU        RCU_GPIOB

#define LED2_PORT       GPIOA
#define LED2_PIN        GPIO_PIN_12
#define LED2_RCU        RCU_GPIOA

#define LED3_PORT       GPIOB
#define LED3_PIN        GPIO_PIN_4
#define LED3_RCU        RCU_GPIOB
```

### Change LED Polarity

Edit:

```c
#define LED_ACTIVE_LOW  0
```

Use `1` if the LEDs are active-low.

---

## Notes

- The phone only writes commands to the BLE service.
- The board responds using BLE notifications.
- Command length is limited by the local receive buffer used in the callback.
- The command buffer is protected against overflow by limiting the copied data length.
- The example uses simple pairing/security settings suitable for a training project.
- The BLE response text is currently written in Portuguese because the command responses in the source code use Portuguese strings.

---

## Troubleshooting

### The device does not appear in the BLE scanner

Check that:

- BLE initialization completed successfully
- Advertising was created and started
- BLE interrupts are enabled
- The board is powered correctly
- No previous phone connection is still active

### The phone connects but commands do not work

Check that:

- The phone is writing to the correct BLE characteristic
- The data is sent as text/ASCII
- The command is one of the supported commands
- The RX callback `app_datatrans_srv_rx_callback()` is registered

### The board receives commands but LEDs do not change

Check that:

- The GPIO mapping matches your board hardware
- The GPIO clocks are enabled
- LED polarity matches `LED_ACTIVE_LOW`
- The LEDs are physically connected to the expected pins

### The board controls LEDs but no response appears in the phone

Check that:

- Notifications are enabled in the BLE terminal application
- The phone is connected before the response is sent
- The connection index is valid
- The data transfer service TX path is working

---

## Educational Summary

This project is a compact example of BLE-to-GPIO control. It demonstrates how a BLE peripheral can receive simple text commands from a smartphone, parse them in firmware, control hardware outputs, and send feedback to the user through BLE notifications.

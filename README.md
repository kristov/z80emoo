# z80emoo - Z80 emulator with serial input+output

I wanted a really simple emulator for simulating a basic RC2014 computer with an SIO serial interface attached at 0x80 and 0x81.

    ./z80emoo -r oz-rc2014.bin

It runs a separate thread for the CPU and in the main thread runs a basic serial interface using ctk (Console Tool Kit). It uses two ring buffers to communicate serial. These emulate input and output buffers in the serial hardware. The `z8mo->in` buffer delivers keypresses from ctk to the CPU via the `z8mo_port_read` emulator callback. The `z8mo->out` buffer delivers bytes written to the port back to ctk via the ctk `loop_event_handler` event loop callback, which writes the characters to a buffer for later rendering.

Pressing a key triggers ctk to deliver the key press to `key_event_handler` (via `main_event_handler`). This writes the key into the `z8mo->in` buffer and then triggers an interrupt on the CPU. Hopefully there is code running on the CPU that will read from the port. This will trigger the `z8mo_port_read` callback that will pull the keypress from the buffer. Then hopefully the code will write that character back to the port (echo) where it is queued on `z8mo->out`. The `loop_event_handler` callback is fired every 50ms.

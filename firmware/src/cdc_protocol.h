/* cdc_protocol.h - the wire protocol the desktop configurator speaks.
 *
 * Deliberately line-based ASCII: you can drive the entire pad from a plain
 * serial terminal, which makes both the firmware and the GUI far easier to
 * debug than an opaque binary framing would.  Binary payloads (icons, profile
 * files) travel as base64 so the transport stays text.
 *
 * Request/response shape:
 *   every command answers with zero or more "+" data lines, then exactly one
 *   terminator: "OK" (optionally with text) or "ERR <reason>".
 *   Lines beginning with "#" are asynchronous log output and may appear at
 *   any time; a client that ignores them still sees a well-formed exchange.
 *
 * Commands:
 *   PING                    identity handshake
 *   INFO                    device, storage and firmware facts
 *   STATUS                  live telemetry (wheel, queues, screen)
 *   GET  <key>              read one setting
 *   SET  <key> <value>      change one setting (applied immediately)
 *   SAVE                    flush settings to the card now
 *   PROFILES                list profile files
 *   PROFILE <file>          make that profile active
 *   RESCAN                  re-read the card and reload the active profile
 *   LIST <dir>              directory listing
 *   READ <path>             file contents, base64
 *   WRITE <path>            base64 lines follow, terminated by "."
 *   DELETE <path>           remove a file
 *   MKDIR <path>            create a directory
 *   TEST <action>           perform one action, e.g. TEST CTRL+C
 *   LOG <off|error|warn|info|debug>
 *   BOOTSEL                 reboot into the USB firmware bootloader
 *   REBOOT                  restart the firmware
 */
#ifndef HP_CDC_PROTOCOL_H
#define HP_CDC_PROTOCOL_H


#ifdef __cplusplus
extern "C" {
#endif
void hp_cdc_init(void);
void hp_cdc_task(void);

#ifdef __cplusplus
}
#endif

#endif /* HP_CDC_PROTOCOL_H */

# Pico 1 controller (prototype)

This Pico SDK project receives ASCII lines from the notebook's USB serial
terminal, applies uncased greedy WordPiece tokenization, broadcasts a fixed
configuration record over UART TX, waits for one READY input, then sends
token IDs to node 1 over SPI master.

## Current assumptions and limits

- `vocab.txt` is a newline-separated vocabulary with IDs equal to zero-based
  line numbers and must include `[UNK]`, `[CLS]`, `[SEP]`.
- Model transfer sizing currently assumes Medium hidden size 512 and FP16
  activations. The configured packet length is `token_count * 512 * 2` bytes.
- Pin definitions are placeholders and must be reconciled with the circuit.
- READY is currently one input because the four-node wired-AND line is the
  planned arrangement. This prototype does not yet implement per-node READY.
- PIO USB keyboard host support is not wired in yet; line input uses Pico USB
  CDC so the tokenizer and protocol flow can be brought up first.
- SPI verify uses a different header today; node firmware must adopt
  `protocol.h` fields before this Pico sender interoperates with it.
- Result reception and retry/error recovery are not implemented yet.

Build with Pico SDK 2.3.1 and its TinyUSB submodule initialized.
Pico-PIO-USB is a separate dependency; set `PICO_PIO_USB_PATH` to its source
directory when configuring CMake if it is not already under TinyUSB. Then build
target `pico1_controller`. Copy the vocabulary file to the configured path in a
way supported by the selected Pico filesystem/embedding workflow; plain host
file paths do not exist on the Pico at runtime.

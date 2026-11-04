# Two ESP32-S3 boards, four toy Transformer layers

This Arduino IDE sketch demonstrates the flow discussed for the project:

```text
toy input vectors -> node 1: layers 0, 1 -> SPI hidden-state frame
                  -> node 2: layers 2, 3 -> tiny [CLS] classification head
```

It is intentionally small so the attention and tensor operations fit in one
readable sketch. The dimensions are 4 tokens, hidden width 8, 2 attention
heads, and FFN width 16. Each layer still performs Q/K/V projections,
scaled dot-product self-attention across all four tokens, output projection,
residual plus LayerNorm, and an expanded GELU feed-forward block with a
second residual plus LayerNorm.

## Wiring and upload

1. Open `two_node_bert_demo.ino` in Arduino IDE and select your ESP32-S3 board.
2. Choose GPIO pins supported by your exact board and edit the five `SPI_*_PIN`
   definitions in the sketch. Use the same SCK, MOSI, MISO, and CS GPIO numbers
   on both boards. READY is node 2's output and node 1's input.
3. Wire node 1 SCK -> node 2 SCK, node 1 MOSI -> node 2 MOSI, node 1 MISO <-
   node 2 MISO, node 1 CS -> node 2 CS, node 2 READY -> node 1 READY, and GND
   -> GND. Do not connect the boards' 3.3 V pins together. The `-1` values in
   the sketch are deliberate placeholders; the program prints a message and
   stops until you set the pins.
4. Upload once with `NODE_ID` set to `2` to the second board.
5. Upload with `NODE_ID` set to `1` to the first board.
6. Open node 2's serial monitor and reset node 2. It arms its SPI receive
   transaction and raises READY. Then reset node 1. Node 1 computes two layers,
   waits for READY, and sends one SPI frame. Node 2 verifies the header and CRC,
   computes its two layers, and prints the toy head logits.

Use 115200 baud for each USB serial monitor. The inter-board link is SPI mode
0 at 1 MHz. READY is a separate GPIO handshake so the master starts clocking
only after the slave has queued its receive buffer.

## Reading the AI flow

- The input vectors stand in for token, position, and segment embeddings.
- In attention, each token's Q vector is compared with every token's K
  vector. Softmax turns those scores into weights used to combine V vectors.
  This is why a node needs the whole sentence tensor before it can finish a
  layer.
- Each layer adds the attention result back to its input and normalizes it,
  then does the same around the FFN. The resulting tensor is still
  `SEQ_LEN x HIDDEN`.
- Node 1 sends all 4 x 8 FP32 values. Node 2 continues from exactly that
  intermediate result; it does not receive the original tokens again.
- The final head reads row zero, the toy `[CLS]` vector, and produces three
  logits. The stable pseudo-weights make repeat runs deterministic, but they
  have not been trained, so the class has no meaningful language meaning.

## Scope

This is a learning example, not the project's BERT firmware. It uses generated
toy weights, an FP32 SPI payload, a small approximate GELU, and no real
tokenizer or learned embedding table. It does not use TinyBERT dimensions,
FP16 packets, the project's device/action/amount heads, or its shared
production protocol. The small SPI transaction uses DMA-capable buffers and a
READY wire to demonstrate the transfer sequence.

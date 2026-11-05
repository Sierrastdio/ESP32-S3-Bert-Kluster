#pragma once

// Assign these to the board pins selected for your wiring.
#define CONFIG_UART_TX_PIN     0
#define CONFIG_SPI_TX_SCK_PIN  2
#define CONFIG_SPI_TX_MOSI_PIN 3
#define CONFIG_SPI_TX_CS_PIN   5
#define CONFIG_SPI_READY_PIN   6

#define CONFIG_UART_BAUD       115200
#define CONFIG_SPI_BAUD        1000000
#define CONFIG_MAX_TOKENS      64
#define CONFIG_HIDDEN_SIZE     512
#define CONFIG_BYTES_PER_VALUE 2
#define CONFIG_MAX_WORD_CHARS  100
#define CONFIG_VOCAB_PATH      "vocab.txt"

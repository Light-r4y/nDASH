#pragma once
#include <stdbool.h>
#include <stdint.h>

// Records the signals of the layout to the microSD card in the MegaLogViewer format (mlg.h): the
// signals of the current layout, LND_LOGGER_PERIOD_MS apart, into log_NNNN.mlg in the root of the
// card. When to record is decided by the settings (settings.h, logger_logic.h) and the buttons of the
// web page "Logs". The card is FAT32, the task mounts it itself (board.h: lnd_board_sd_mount).

typedef enum {
  LND_LOG_NO_CARD = 0,  // no card, or it did not mount
  LND_LOG_IDLE,         // the card is ready, nothing is recorded
  LND_LOG_RECORDING,
} lnd_log_state_t;

typedef enum {
  LND_LOG_ERR_NONE = 0,
  LND_LOG_ERR_MOUNT,      // the card did not mount (no card, not FAT32, does not answer)
  LND_LOG_ERR_OPEN,       // a file could not be created
  LND_LOG_ERR_WRITE,      // a write error (the card was taken out?)
  LND_LOG_ERR_FULL,       // no space left
  LND_LOG_ERR_NO_SIGNALS  // the layout has no signals
} lnd_log_error_t;

typedef struct {
  lnd_log_state_t state;
  lnd_log_error_t error;  // why the last recording stopped or failed; cleared by the next start
  char file[16];          // the current (or the last) file, "" - none
  uint32_t records;
  uint32_t bytes;
  uint32_t seconds;       // how long it has been recording
  uint64_t total_kb, free_kb;  // 0 if there is no card
} lnd_log_status_t;

void lnd_logger_task_start(void);
void lnd_logger_get_status(lnd_log_status_t *out);

// Requests from the web page: done by logger_task within one tick (LND_LOGGER_PERIOD_MS).
void lnd_logger_request_start(void);
void lnd_logger_request_stop(void);
// Try to mount the card now (in the "manual" mode it is not looked for by itself).
void lnd_logger_request_rescan(void);

// The root of the card (LND_LOG_MOUNT_PATH) if it is mounted, else NULL. For the file list and download.
const char *lnd_logger_root(void);

// A log file name such as log_0001.mlg (in any case).
bool lnd_logger_valid_name(const char *name);

// The card disappeared when it was accessed (a folder did not open): unmount it on the next tick.
void lnd_logger_card_lost(void);

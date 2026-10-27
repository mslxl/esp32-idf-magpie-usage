#ifndef NOTE4_UI_H
#define NOTE4_UI_H
#include <stddef.h>
#include <stdint.h>

#define NOTE4_UI_FRAME_BYTES 15000U

/* Borrowed card fields. Every pointer may be NULL and must stay valid for the
 * duration of build. No credentials enter this surface. Field order mirrors
 * the Rust repr(C) structs exactly. */
typedef struct {
    const char *provider, *name, *user, *plan, *alias, *seat, *from, *kind;
    const char *balance, *until, *renew, *as_of, *read_at;
    uint32_t read_deferred, failed;
    size_t windows, balances;
} note4_ui_card_t;

typedef struct {
    const char *name, *display, *used, *remaining, *resets_at, *reset_secs;
    const char *amount, *limit, *unit, *pool, *holds_why;
    uint32_t unlimited;
} note4_ui_window_t;

typedef struct {
    const char *label, *text;
    uint32_t failed;
} note4_ui_balance_t;

/* Immutable snapshot access; each callback fills one struct per item. */
typedef struct {
    const void *context;
    size_t count;
    void (*card)(const void *, size_t card, note4_ui_card_t *out);
    void (*window)(const void *, size_t card, size_t child, note4_ui_window_t *out);
    void (*balance)(const void *, size_t card, size_t part, note4_ui_balance_t *out);
    uint32_t (*visible)(const void *, size_t card);
    size_t (*rank)(const void *, size_t card);
    uint32_t smart_order;
} note4_ui_source_t;

typedef struct {
    uint32_t state, slot, round, storage_error, pairing, passkey;
    uint32_t phase, result, error_page, reading, retry_seconds;
    uint32_t layout, adaptive;
    int64_t now;
    const char *timezone;
    const char *ble_name;
    /* Flags: bit 0 calibrated reading valid, bit 1 charging, bit 2 full. */
    uint32_t battery_percent, battery_flags;
    /* 0 English, 1 Chinese. */
    uint32_t language;
    /* Seconds until the next automatic Wi-Fi retry; 0 when none is scheduled. */
    uint32_t wifi_retry_seconds;
} note4_ui_status_t;

typedef struct note4_ui note4_ui_t;
size_t note4_ui_size(void);
size_t note4_ui_align(void);
void note4_ui_init(note4_ui_t *ui);
/* Returns 0 only for a complete layout; overflow rejects the entire candidate. */
int note4_ui_build(note4_ui_t *ui, const note4_ui_source_t *source, const note4_ui_status_t *status);
uint32_t note4_ui_pages(const note4_ui_t *ui);
uint32_t note4_ui_used(const note4_ui_t *ui);
/* Epoch second at which the built text goes stale (a reset passes or the local
 * date changes). Values at or before the 2000 epoch mean "rebuild once the
 * clock is synchronised"; INT64_MAX means never. */
int64_t note4_ui_next_change(const note4_ui_t *ui);
/* Draws exactly one page, with status overlays, into a 400x300 1bpp frame
 * (1 = black, MSB first, 50 bytes per row; length NOTE4_UI_FRAME_BYTES). */
int note4_ui_draw(const note4_ui_t *ui, const note4_ui_status_t *status,
                  uint32_t page, uint32_t available, uint8_t *frame, size_t length);
#endif

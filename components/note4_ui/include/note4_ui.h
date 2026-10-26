#ifndef NOTE4_UI_H
#define NOTE4_UI_H
#include <stddef.h>
#include <stdint.h>

/* Immutable snapshot access. Strings remain borrowed for the duration of build.
 * section: 0=card, 1=window, 2=balance part. No credentials enter this surface. */
typedef struct {
    const void *context;
    size_t count;
    const char *(*text)(const void *, size_t card, uint32_t section, size_t child, const char *field);
    size_t (*children)(const void *, size_t card, uint32_t section);
    uint32_t (*visible)(const void *, size_t card);
    size_t (*rank)(const void *, size_t card);
} note4_ui_source_t;

typedef struct {
    uint32_t state, slot, round, storage_error, pairing, passkey;
    uint32_t phase, result, error_page, reading, retry_seconds;
    uint32_t layout, adaptive;
    int64_t now;
    const char *timezone;
    const char *ble_name;
} note4_ui_status_t;

typedef struct note4_ui note4_ui_t;
size_t note4_ui_size(void);
size_t note4_ui_align(void);
void note4_ui_init(note4_ui_t *ui);
/* Returns 0 only for a complete layout; overflow rejects the entire candidate. */
int note4_ui_build(note4_ui_t *ui, const note4_ui_source_t *source, const note4_ui_status_t *status);
uint32_t note4_ui_pages(const note4_ui_t *ui);
uint32_t note4_ui_used(const note4_ui_t *ui);
/* Draws exactly one page, with status overlays, into a 400x300 RGB565 buffer. */
int note4_ui_draw(const note4_ui_t *ui, const note4_ui_status_t *status,
                  uint32_t page, uint32_t available, uint16_t *pixels, size_t length);
#endif

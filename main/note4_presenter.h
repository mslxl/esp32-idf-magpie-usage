#ifndef NOTE4_PRESENTER_H
#define NOTE4_PRESENTER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "zectrix_epd.h"

#include "display_adapter.h"

/* Partial refreshes accumulate ghosting; force a full refresh after this many
 * partial waveform passes or this long since the last full refresh. */
#define NOTE4_PRESENTER_MAX_PARTIALS 30U
#define NOTE4_PRESENTER_FULL_INTERVAL_US (60LL * 60LL * 1000000LL)

typedef struct note4_presenter note4_presenter_t;

typedef enum {
    NOTE4_PRESENT_NONE = 0,
    NOTE4_PRESENT_PARTIAL,
    NOTE4_PRESENT_FULL,
} note4_present_mode_t;

esp_err_t note4_presenter_create(note4_presenter_t **out_presenter);
void note4_presenter_destroy(note4_presenter_t *presenter);

/* Draw target: 400x300 1bpp, 1 = black, NOTE4_FRAME_BYTES long. */
uint8_t *note4_presenter_frame(note4_presenter_t *presenter);
/* What the panel shows, in the same format; valid only after a success. */
const uint8_t *note4_presenter_shown(const note4_presenter_t *presenter);

/* Row-band damage between the draw target and the panel. Returns one full
 * rectangle when the panel has no known content. */
size_t note4_presenter_damage(const note4_presenter_t *presenter,
                              note4_rect_t *rects, size_t max_rects);

/*
 * Presents the draw target. With no damage and a known panel this is a no-op
 * (mode NONE). Otherwise a full refresh is used when requested, when the panel
 * content is unknown, when the partial budget would be exceeded or when the
 * last full refresh is older than NOTE4_PRESENTER_FULL_INTERVAL_US; else each
 * byte-aligned damage rectangle gets one partial pass. Any driver failure
 * marks the panel content unknown.
 */
esp_err_t note4_presenter_present(
    note4_presenter_t *presenter, zectrix_epd_handle_t epd,
    const note4_rect_t *damage, size_t damage_count, bool full_redraw,
    int64_t now_us, note4_present_mode_t *out_mode);

#endif

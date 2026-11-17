#include "capture.h"
#include "timing.h"
#include "stats.h"
#include "hw_defs.h"
#include "stm32f103_md.h"

#define CAP_FLAG_MARK 0x08u

static uint8_t events[CAPTURE_MAX_EVENTS * CAPTURE_EVT_SIZE];
static volatile uint16_t n_events;
static volatile uint16_t n_dropped;
static uint16_t limit_events;
static volatile uint8_t running;
static volatile uint8_t chan_mask;

void capture_init(void)
{
    RCC.APB2ENR |= RCC_APB2ENR_AFIOEN;
    /* EXTI lines 6/7 from GPIOB. */
    AFIO.EXTICR[1] &= ~(0xFu << (4u * 2u)) & ~(0xFu << (4u * 3u));
    AFIO.EXTICR[1] |= (0x1u << (4u * 2u)) | (0x1u << (4u * 3u));

    EXTI.IMR  &= ~((1u << 6u) | (1u << 7u));
    EXTI.RTSR |=  (1u << 6u) | (1u << 7u);
    EXTI.FTSR |=  (1u << 6u) | (1u << 7u);
    EXTI.PR    =  (1u << 6u) | (1u << 7u);
    NVIC_EnableIRQ(EXTI9_5_IRQn);
}

void capture_start(uint8_t channel_mask, uint16_t max_events)
{
    n_events = 0u;
    n_dropped = 0u;
    limit_events = max_events ? max_events : CAPTURE_MAX_EVENTS;
    if (limit_events > CAPTURE_MAX_EVENTS) limit_events = CAPTURE_MAX_EVENTS;
    chan_mask = channel_mask;
    running = 1u;
    EXTI.PR = (1u << 6u) | (1u << 7u);
    EXTI.IMR |= (1u << 6u) | (1u << 7u);
}

void capture_stop(void)
{
    running = 0u;
    EXTI.IMR &= ~((1u << 6u) | (1u << 7u));
}

static void push_event(uint32_t ts, uint8_t flags)
{
    if (!running) return;
    if (n_events >= limit_events) {
        if (n_dropped < 0xFFFFu) n_dropped++;
        stats_inc(STAT_OVERFLOW);
        return;
    }
    uint16_t o = (uint16_t)(n_events * CAPTURE_EVT_SIZE);
    events[o + 0] = (uint8_t)(ts & 0xFFu);
    events[o + 1] = (uint8_t)((ts >> 8)  & 0xFFu);
    events[o + 2] = (uint8_t)((ts >> 16) & 0xFFu);
    events[o + 3] = (uint8_t)((ts >> 24) & 0xFFu);
    events[o + 4] = flags;
    n_events++;
}

void capture_mark(uint8_t label)
{
    push_event(timing_us(), (uint8_t)(CAP_FLAG_MARK | (label & 0x07u)));
}

void EXTI9_5_IRQHandler(void)
{
    uint32_t pr = EXTI.PR & ((1u << 6u) | (1u << 7u));
    if (!pr) return;
    EXTI.PR = pr;

    uint32_t idr = (uint32_t)(GPIOB.IDR & (GPIO_PIN_6 | GPIO_PIN_7));
    uint32_t ts = timing_us();
    if ((pr & (1u << 6u)) && (chan_mask & CAP_FLAG_SCL)) {
        uint8_t rising = (idr & GPIO_PIN_6) ? CAP_FLAG_EDGE : 0u;
        push_event(ts, (uint8_t)(CAP_FLAG_SCL | rising));
    }
    if ((pr & (1u << 7u)) && (chan_mask & CAP_FLAG_SDA)) {
        uint8_t rising = (idr & GPIO_PIN_7) ? CAP_FLAG_EDGE : 0u;
        push_event(ts, (uint8_t)(CAP_FLAG_SDA | rising));
    }
}

uint16_t capture_get(uint8_t *buf, uint16_t offset, uint16_t max_bytes,
                     uint16_t *total_events)
{
    uint16_t total_bytes = (uint16_t)(n_events * CAPTURE_EVT_SIZE);
    if (total_events) *total_events = n_events;
    if (offset >= total_bytes) return 0u;
    uint16_t n = (uint16_t)(total_bytes - offset);
    if (n > max_bytes) n = max_bytes;
    for (uint16_t i = 0; i < n; i++) buf[i] = events[offset + i];
    return n;
}

uint16_t capture_count(void)  { return n_events; }
uint16_t capture_dropped(void) { return n_dropped; }

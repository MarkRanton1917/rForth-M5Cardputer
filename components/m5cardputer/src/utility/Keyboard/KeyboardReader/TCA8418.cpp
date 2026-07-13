/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "TCA8418.h"
#include "../../common.h"
#include "../../Adafruit_TCA8418/Adafruit_TCA8418_registers.h"
#include <Arduino.h>
#include <M5Unified.h>

// Default interrupt pin for M5Cardputer ADV
#define DEFAULT_TCA8418_INT_PIN 11

// Sentinel value marking an invalid / discarded key event
static constexpr uint8_t INVALID_ROW = 0xFF;

TCA8418KeyboardReader::TCA8418KeyboardReader(int interrupt_pin) : _isr_flag(false), _interrupt_pin(interrupt_pin)
{
    if (_interrupt_pin < 0) {
        _interrupt_pin = DEFAULT_TCA8418_INT_PIN;
    }
}

void IRAM_ATTR TCA8418KeyboardReader::gpio_isr_handler(void* arg)
{
    TCA8418KeyboardReader* reader = static_cast<TCA8418KeyboardReader*>(arg);
    reader->_isr_flag             = true;
}

void TCA8418KeyboardReader::begin()
{
    // Initialize TCA8418
    _tca8418 = std::make_unique<Adafruit_TCA8418>();

    if (!_tca8418->begin()) {
        printf("[error] TCA8418KeyboardReader: Failed to initialize TCA8418\n");
        return;
    }

    // Setup keypad matrix
    _tca8418->matrix(7, 8);

    // IMPORTANT: matrix() only touches KP_GPIO_1..3 (which pins belong to
    // the matrix). The pins used by the matrix are still left configured
    // as GPIO-event/interrupt sources from Adafruit_TCA8418::begin()
    // (GPI_EM = 0xFF, GPIO_INT_EN = 0xFF). That means matrix pins can
    // raise a GPI interrupt (bit 0x02 in INT_STAT) in addition to the
    // normal key event interrupt (bit 0x01) -- and GPI_INT is never
    // cleared anywhere, so the INT line can get stuck low once it fires.
    // Disable GPIO-event/interrupt generation entirely; we only care
    // about keypad matrix events here.
    _tca8418->writeRegister8(TCA8418_REG_GPI_EM_1, 0x00);
    _tca8418->writeRegister8(TCA8418_REG_GPI_EM_2, 0x00);
    _tca8418->writeRegister8(TCA8418_REG_GPI_EM_3, 0x00);
    _tca8418->writeRegister8(TCA8418_REG_GPIO_INT_EN_1, 0x00);
    _tca8418->writeRegister8(TCA8418_REG_GPIO_INT_EN_2, 0x00);
    _tca8418->writeRegister8(TCA8418_REG_GPIO_INT_EN_3, 0x00);

    _tca8418->flush();

    // Setup interrupt pin
    if (_interrupt_pin >= 0) {
        pinMode(_interrupt_pin, INPUT);
        attachInterruptArg(digitalPinToInterrupt(_interrupt_pin), gpio_isr_handler, this, CHANGE);
    }

    // Enable interrupts
    _tca8418->enableInterrupts();
}

void TCA8418KeyboardReader::update()
{
    if (!_isr_flag) {
        return;
    }

    while (true) {
        uint8_t raw = _tca8418->getEvent();

        if (raw == 0) {
            break;
        }

        KeyEventRaw_t event = get_key_event_raw(raw);

        if (event.row == INVALID_ROW) {
            continue;
        }

        remap(event);
        update_key_list(event);
    }

    _tca8418->writeRegister8(TCA8418_REG_INT_STAT, 0x1F);

    if ((_tca8418->readRegister8(TCA8418_REG_INT_STAT) & 0x01) == 0) {
        _isr_flag = false;
    }
}

TCA8418KeyboardReader::KeyEventRaw_t TCA8418KeyboardReader::get_key_event_raw(const uint8_t& eventRaw)
{
    KeyEventRaw_t ret;

    // 0x00 means "no event available" -- getEvent() can legitimately
    // return this, e.g. if update() is invoked without a real pending
    // event. Treat it as invalid rather than letting the row/col math
    // below produce a wraparound value.
    if (eventRaw == 0x00) {
        ret.row = INVALID_ROW;
        return ret;
    }

    ret.state       = eventRaw & 0x80;
    uint16_t buffer = eventRaw & 0x7F;
    buffer--;  // event codes are 1-based

    uint8_t row = buffer / 10;
    uint8_t col = buffer % 10;

    // matrix(7, 8) means only row 0..6 and col 0..7 are valid. Anything
    // outside that range is a corrupt/spurious event (can happen under
    // electrical stress from many simultaneous key contacts) and must be
    // discarded rather than passed on to remap()/update_key_list(), where
    // it would eventually index out of bounds into _key_value_map[4][14]
    // in Keyboard.cpp.
    if (row > 6 || col > 7) {
        ret.row = INVALID_ROW;
        return ret;
    }

    ret.row = row;
    ret.col = col;
    return ret;
}

// Remap to the same layout as cardputer
void TCA8418KeyboardReader::remap(KeyEventRaw_t& key)
{
    // Col
    uint8_t col = 0;
    col         = key.row * 2;
    if (key.col > 3) col++;

    // Row
    uint8_t row = 0;
    row         = (key.col + 4) % 4;

    key.row = row;
    key.col = col;
}

void TCA8418KeyboardReader::update_key_list(const KeyEventRaw_t& eventRaw)
{
    // Defensive: should already be filtered out in update(), but keep this
    // as a safety net in case update_key_list() is ever called directly.
    if (eventRaw.row == INVALID_ROW) {
        return;
    }

    Point2D_t point;
    point.x = eventRaw.col;
    point.y = eventRaw.row;

    // Add or remove the key from the list
    if (eventRaw.state) {
        auto it = std::find(_key_list.begin(), _key_list.end(), point);
        if (it == _key_list.end()) {
            _key_list.push_back(point);
        }
    } else {
        auto it = std::find(_key_list.begin(), _key_list.end(), point);
        if (it != _key_list.end()) {
            _key_list.erase(it);
        }
    }
}
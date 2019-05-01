#include <stdio.h>
#include <alsa/asoundlib.h>
#include <pthread.h>

#include "../events.h"

#include "device.h"
#include "device_midi.h"
#include "../clocks/clock_midi.h"

static int perr(char const *format, ...) {
    va_list args;
    int r;

    va_start (args, format);
    r = vfprintf(stderr, format, args);
    va_end(args);

    return r;
}

int dev_midi_init(void *self) {
    struct dev_midi *midi = (struct dev_midi *) self;
    struct dev_common *base = (struct dev_common *) self;

    unsigned int alsa_card;
    unsigned int alsa_dev;
    char *alsa_name;

    sscanf(base->path, "/dev/snd/midiC%uD%u", &alsa_card, &alsa_dev);

    if (asprintf(&alsa_name, "hw:%u,%u", alsa_card, alsa_dev) < 0) {
        fprintf(stderr, "failed to create alsa device name for card %d,%d\n", alsa_card, alsa_dev);
        return -1;
    }

    if (snd_rawmidi_open(&midi->handle_in, &midi->handle_out, alsa_name, 0) < 0) {
        fprintf(stderr, "failed to open alsa device %s\n", alsa_name);
        return -1;
    }

    base->start = &dev_midi_start;
    base->deinit = &dev_midi_deinit;

    return 0;
}

void dev_midi_deinit(void *self) {
    struct dev_midi *midi = (struct dev_midi *) self;
    snd_rawmidi_close(midi->handle_in);
    snd_rawmidi_close(midi->handle_out);
}

void* dev_midi_start(void *self) {
    struct dev_midi *midi = (struct dev_midi *) self;
    union event_data *ev;

    ssize_t read = 0;
    uint8_t byte = 0;
    uint8_t msg_buf[256];
    uint8_t msg_pos = 0;
    uint8_t msg_len = 0;

    do {
        read = snd_rawmidi_read(midi->handle_in, &byte, 1);
        if (byte >= 0xf8) {
            clock_midi_handle_message(byte);

            ev = event_data_new(EVENT_MIDI_EVENT);
            ev->midi_event.id = midi->dev.id;
            ev->midi_event.data[0] = byte;
            ev->midi_event.nbytes = 1;
            event_post(ev);
        } else {
            if (byte >= 0x80) {
                msg_buf[0] = byte;
                msg_pos = 1;

                switch (byte & 0xf0) {
                case 0x80:
                case 0x90:
                case 0xa0:
                case 0xb0:
                case 0xe0:
                case 0xf2:
                    msg_len = 3;
                    break;
                case 0xc0:
                case 0xd0:
                    msg_len = 2;
                    break;
                case 0xf0:
                    switch (byte & 0x0f) {
                    case 0x01:
                    case 0x03:
                        msg_len = 2;
                        break;
                    case 0x07:
                        // TODO: properly handle sysex length
                        msg_len = msg_pos; // sysex end
                        break;
                    case 0x00:
                        msg_len = 0; // sysex start
                        break;
                    default:
                        msg_len = 2;
                        break;
                    }
                    break;
                default:
                    msg_len = 2;
                    break;
                }
            } else {
                msg_buf[msg_pos] = byte;
                msg_pos += 1;
            }

            if (msg_pos == msg_len) {
		int8_t ch = msg_buf[0] & 0x0F;
    		int8_t cc = msg_len>1 ? msg_buf[1] : 0;
		perr("Got MIDI event on channel %d\n", ch);

		if (msg_len>2 && ch == 14 && cc>=21 && cc<=26) {
          	  int8_t data = msg_buf[2];

		  if(cc>=24 && cc<=26){
		    int8_t btn = msg_buf[1] - 24;
		    ev = event_data_new(EVENT_KEY);
		    ev->enc.n = btn + 1;
		    ev->enc.delta = (data==127 ? -1 : (data>0));
		    perr("norns button %d %d\n", ev->enc.n, ev->enc.delta);
		  } else {
		    perr("MIDI 15 encoder %d %d\n", cc, data);
		    const int steps = 1;
                    float value = data & 0x40
                                  ? (128.0f - (float) data) / (128.0f * steps) * -1.0f
                                  : ((float) data) / (128.0f * steps);
		    ev = event_data_new(EVENT_ENC);
		    ev->enc.n = cc - 21 + 1;
		    ev->enc.delta = (value == 0 ? 0 : (value > 0 ? 1 : -1) );
		    perr("norns encoder %d %d\n", ev->enc.n, ev->enc.delta);
		  }
		} else {
                  ev = event_data_new(EVENT_MIDI_EVENT);
                  ev->midi_event.id = midi->dev.id;
                  ev->midi_event.data[0] = msg_buf[0];
                  ev->midi_event.data[1] = msg_len > 1 ? msg_buf[1] : 0;
                  ev->midi_event.data[2] = msg_len > 2 ? msg_buf[2] : 0;
                  ev->midi_event.nbytes = msg_len;
		}

                event_post(ev);

                msg_pos = 0;
                msg_len = 0;
            }
        }
    } while (read > 0);

    return NULL;
}

ssize_t dev_midi_send(void *self, uint8_t *data, size_t n) {
    struct dev_midi *midi = (struct dev_midi *) self;
    return snd_rawmidi_write(midi->handle_out, data, n);
}

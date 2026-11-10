#ifndef MIDI_H_
#define MIDI_H_

#include "usb.h"
#include "usb_device.h"
#include "usb_device_class.h"

#include <stdint.h>

typedef struct _usb_midi_struct_t
{
    usb_device_handle deviceHandle;
    class_handle_t midiHandle;
} usb_midi_struct_t;

typedef struct
{
    uint8_t type;
    uint8_t data1;
    uint8_t data2;
    uint8_t channel;
} MidiUsbMessage;

enum MidiMessageType
{
    kNoteOff = 0x80,
    kNoteOn = 0x90,
    kAfterTouch = 0xA0,
    kControlChange = 0xB0,
    kProgramChange = 0xC0,
    kChannelPressure = 0xD0,
    kPitchBend = 0xE0,
};

#ifdef __cplusplus
extern "C"
{
#endif

    bool midiUsbAvailable();
    void midiUsbSend(MidiUsbMessage* midiMessage);
    bool midiUsbReceive(MidiUsbMessage* midiMessage);
    // copies up to maxBytes received SysEx bytes (F0 ... F7) into dst; returns how many
    uint32_t midiUsbReadSysex(uint8_t* dst, uint32_t maxBytes);

#ifdef __cplusplus
}
#endif

void midiUsbTx(uint32_t packet);

#endif /* MIDI_H_ */

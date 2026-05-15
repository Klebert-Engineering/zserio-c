// Minimal telemetry frame schema for the embedded-decode example.
//
// Shape:
//   - 32-bit sensor identifier
//   - variable-length human-readable device label
//   - 16-bit reading count
//   - that many signed 16-bit samples
//
// Small enough to fit fully on the stack at decode time except for
// the device_label (one alloc) and the readings array (one alloc),
// both of which the example funnels through the runtime's reference
// bump arena.

package telemetry;

struct Frame
{
    uint32  sensor_id;
    string  device_label;
    uint16  reading_count;
    int16   readings[reading_count];
};

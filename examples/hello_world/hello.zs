// Running example for the zserio-c generator's end-to-end smoke
// test. Each batch of generator work that adds a new construct
// here also adds a smoke check to tools/smoke_generator.sh.

package hello;

const uint8  MAX_AGE        = 150;
const uint32 MAX_NAME_LENGTH = 32;
const bool   ENABLE_BONUS   = true;

subtype uint16 BlockIndex;

enum bit:3 Color
{
    NONE  = 0,
    RED   = 2,
    BLUE,
    BLACK = 7
};

bitmask uint8 Permission
{
    READ,
    WRITE,
    EXECUTE
};

struct Address
{
    uint16  street_no;
    string  street;
};

union Greeting
{
    string      text_greeting;
    uint32      coded_greeting;
    Address     address_greeting;
};

choice Variant(uint8 t) on t
{
    case 0:
        uint16  a;
    case 1:
    case 2:
        uint32  b;
    case 3:
        string  c;
};

struct Container
{
    uint8        tag;
    Variant(tag) variant;
};

struct Profile
{
    string             name;
    optional uint8     age;
    optional string    nickname;
    optional Address   work_address;
};

struct Telemetry
{
    uint8              readings[];
    int16              fixed_history[8];
    optional uint32    optional_samples[];
};

struct Catalog
{
    string             items[];
    bytes              chunks[];
    extern             flags[];
    Address            addresses[];
    Address            history[2];
};

struct LayerData(uint32 byte_count)
{
    uint8              header[2];
    uint8              payload[byte_count - 2];
};

struct PowerOfTwoBlock(uint32 shift_amt)
{
    uint8              data[1 << shift_amt];
};

struct Frame
{
    uint32                  size;
    LayerData(size)         layer;
    uint32                  scale;
    PowerOfTwoBlock(scale)  power;
};

struct Validated
{
    uint8                   grade;
    uint8                   passing : passing == 1;
    uint16                  bonus if grade >= 90;
};

struct EnumValidated
{
    Color                   pick;
    uint8                   ranking : pick != Color.NONE;
    uint16                  fallback if pick == Color.RED;
};

choice WithDefault(uint8 sel) on sel
{
    case 0:
        uint16  small;
    case 1:
        uint32  big;
    default:
        string  fallback;
};

union Tagged(uint32 marker)
{
    uint16  small;
    uint32  large;
};

struct UseTagged
{
    uint32          marker;
    Tagged(marker)  payload;
};

struct Hello
{
    uint8       age;
    string      name;
    uint16      salary;
    bit:5       level;
    int:7       offset;
    Color       color;
    Permission  perms;
    BlockIndex  block_idx;
    Address     home;
    bytes       payload;
    extern      raw_bits;
    varuint32   item_count;
    varsize     blob_size;
};

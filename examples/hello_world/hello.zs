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

struct Hello
{
    uint8   age;
    string  name;
    uint16  salary;
};

// Smallest non-empty zserio schema. Used by the Phase 2 generator
// smoke test to confirm ZserioTool loads our extension cleanly.
// Future phases will use this as the running example for end-to-end
// generated-code validation.

package hello;

struct Hello
{
    uint8   age;
    string  name;
    uint16  salary;
};

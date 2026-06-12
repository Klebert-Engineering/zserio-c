# Getting started

Schema → generated C → working executable, end to end. Verified on
the current tree; the bytes counts and output strings below are
what the example actually produces.

## Prerequisites

- C99 toolchain (gcc ≥ 7 or clang ≥ 10)
- CMake ≥ 3.16
- Ninja or GNU Make
- Python 3 (only for the cross-validation harness, not for using
  the generator)

No JDK or Maven is needed to use `zserio_c_gen`. The Java generator
extension under `generator/` is a separate, optional component.

## 1. Build the generator and runtime

```sh
git clone https://github.com/Klebert-Engineering/zserio-c.git
cd zserio-c
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Produces:

- `build/parser/zserio_c_gen` — the CLI generator
- `runtime/include/zserio/*.h` — public runtime headers
- `runtime/src/*.c` — runtime sources to compile alongside your
  generated code

## 2. Write a schema

```zserio
// hello.zs
package hello;

struct Greeting
{
    uint32 messageId;
    string text;
};
```

Schemas are plain text in the zserio language. See
[Schema language](user-guide/schema-language.md) for the supported
constructs; the [upstream language reference](https://github.com/ndsev/zserio/blob/master/doc/ZserioLanguageOverview.md)
is the normative description.

## 3. Generate C

```sh
mkdir -p out
build/parser/zserio_c_gen hello.zs -c out
```

Writes:

```
out/
└── hello/
    ├── greeting.h
    └── greeting.c
```

The generator follows the schema's `package` declarations and
produces one sub-directory per package level. See
[`zserio_c_gen` CLI reference](cli.md) for `-src` import roots and
`-setTopLevelPackage`.

## 4. Use the generated code

```c
// app.c
#include <stdio.h>
#include <string.h>

#include <zserio/allocator.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>

#include "hello/greeting.h"

int main(void)
{
    const zserio_allocator_t alloc = zserio_allocator_libc();

    /* Encode */
    hello_greeting_t out_msg;
    hello_greeting_init(&out_msg);
    out_msg.message_id  = 0xCAFEBABEu;
    const char text[]   = "hello, world";
    out_msg.text.bytes  = (char *)text;
    out_msg.text.length = sizeof text - 1u;

    uint8_t buf[64];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof buf * 8u);
    if (hello_greeting_write(&out_msg, &w) != ZSERIO_OK) {
        return 1;
    }
    const size_t n_bytes =
        (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    printf("encoded %zu bytes\n", n_bytes);

    /* Decode */
    hello_greeting_t in_msg;
    hello_greeting_init(&in_msg);
    zserio_bitstream_reader_t r;
    zserio_bitstream_reader_init(&r, buf, n_bytes * 8u);
    if (hello_greeting_read(&in_msg, &r, &alloc) != ZSERIO_OK) {
        return 1;
    }
    printf("messageId=0x%08x text='%.*s'\n",
           in_msg.message_id,
           (int)in_msg.text.length, in_msg.text.bytes);

    hello_greeting_free(&in_msg, &alloc);
    return 0;
}
```

## 5. Build and run

```sh
gcc -std=c99 -Wall -Wextra \
    -Iruntime/include -Iout \
    app.c out/hello/greeting.c runtime/src/*.c \
    -o app
./app
```

Output:

```
encoded 17 bytes
messageId=0xcafebabe text='hello, world'
```

The 17 bytes break down as 4 bytes for `messageId`, 1 byte for the
varsize string length (12), and 12 bytes for the UTF-8 string.

## What just happened

| Step | What it did |
|---|---|
| `hello_greeting_init` | Zero-initialized the struct. Always call before `_read` or before populating to write. |
| `hello_greeting_write` | Walked the schema's field order, encoded each field through the bitstream writer. No allocation. |
| `hello_greeting_read` | Walked the schema's field order, decoded each field through the bitstream reader. String storage allocated via `alloc`. |
| `hello_greeting_free` | Released the string storage. Always pair `_read` with `_free`. |

The generated type, function names, and member field layout follow
deterministic rules — see [Generated code](user-guide/generated-code.md)
for the full conventions.

## Next

- [Concepts](user-guide/concepts.md) — what a zserio schema *is*
  and what it commits you to.
- [Build integration](user-guide/build-integration.md) — wiring
  this into a real CMake / Make project.
- [Allocators](user-guide/allocators.md) — replace `libc` with a
  bounded arena for embedded use.

/* 30-field kitchen sink of packed arrays. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include <zserio/string.h>
#include "array_types/packed_arrays_mapping/packed_arrays_mapping.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    array_types_packed_arrays_mapping_packed_arrays_mapping_t pkg;
    array_types_packed_arrays_mapping_packed_arrays_mapping_init(&pkg);

    pkg.uint8_value = 4u;

    for (int i = 0; i < 5; ++i) {
        pkg.uint8_array[i] = (uint8_t)(seed + (unsigned)i);
        pkg.uint16_array[i] = (uint16_t)(seed + (unsigned)i);
        pkg.uint32_array[i] = (uint32_t)(seed + (unsigned)i);
        pkg.uint64_array[i] = (uint64_t)(seed + (unsigned)i);
        pkg.int8_array[i] = (int8_t)((int)(seed + (unsigned)i));
        pkg.int16_array[i] = (int16_t)((int)(seed + (unsigned)i));
        pkg.int32_array[i] = (int32_t)((int)(seed + (unsigned)i));
        pkg.int64_array[i] = (int64_t)((int64_t)(seed + (unsigned)i));
        pkg.bitfield8_array[i] = (uint8_t)((seed + (unsigned)i) & 0xFFu);
        pkg.bitfield16_array[i] = (uint16_t)((seed + (unsigned)i));
        pkg.bitfield32_array[i] = (uint32_t)(seed + (unsigned)i);
        pkg.bitfield63_array[i] = (uint64_t)(seed + (unsigned)i);
        pkg.variable_bitfield_long_array[i] = (uint64_t)((seed + (unsigned)i) & 0x0Fu);
        pkg.intfield8_array[i] = (int8_t)((int)(seed + (unsigned)i));
        pkg.intfield16_array[i] = (int16_t)((int)(seed + (unsigned)i));
        pkg.intfield32_array[i] = (int32_t)((int)(seed + (unsigned)i));
        pkg.intfield64_array[i] = (int64_t)((int64_t)(seed + (unsigned)i));
        pkg.variable_intfield_long_array[i] = (int64_t)((int)i - 1);
        pkg.varuint16_array[i] = (uint16_t)(seed + (unsigned)i);
        pkg.varuint32_array[i] = (uint32_t)(seed + (unsigned)i);
        pkg.varuint64_array[i] = (uint64_t)(seed + (unsigned)i);
        pkg.varuint_array[i] = (uint64_t)(seed + (unsigned)i);
        pkg.varsize_array[i] = (uint32_t)(seed + (unsigned)i);
        pkg.varint16_array[i] = (int16_t)((int)(seed + (unsigned)i));
        pkg.varint32_array[i] = (int32_t)((int)(seed + (unsigned)i));
        pkg.varint64_array[i] = (int64_t)((int64_t)(seed + (unsigned)i));
        pkg.varint_array[i] = (int64_t)((int64_t)(seed + (unsigned)i));
        pkg.compound_array[i].id = (uint32_t)(seed + (unsigned)i);
        static char names[5][32];
        int n = snprintf(names[i], sizeof(names[i]), "n%u%d", seed, i);
        pkg.compound_array[i].name.bytes = names[i];
        pkg.compound_array[i].name.length = (size_t)n;
        pkg.enum_array[i] = (array_types_packed_arrays_mapping_test_enum_t)((seed + (unsigned)i) % 3u);
        pkg.bitmask_array[i] = (array_types_packed_arrays_mapping_test_bitmask_t)0u;
    }

    uint8_t buf[1024];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = array_types_packed_arrays_mapping_packed_arrays_mapping_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu\n", seed, byte_pos);
    return 0;
}

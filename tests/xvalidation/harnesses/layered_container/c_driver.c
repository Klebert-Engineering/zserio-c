/* Synthetic production-shape payload: a container of three layers
 * behind an indexed-offset array, each layer dispatching through an
 * enum choice — features (parameterized records, dynamic bitfield,
 * packed GeoPoint path), attributes (packed pairs with a union), and
 * an extern blob. Exercises the combination of features a real
 * layered map-tile payload uses. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>
#include "layered_container/layered_container.h"

int main(int argc, char *argv[])
{
    if (argc < 3) { fprintf(stderr, "usage: %s <seed> <out.bin>\n", argv[0]); return 2; }
    const unsigned seed = (unsigned)atoi(argv[1]);
    layered_container_layered_container_t pkg;
    layered_container_layered_container_init(&pkg);

    static uint32_t offsets[3];
    static layered_container_layer_t layers[3];
    memset(layers, 0, sizeof(layers));

    pkg.container_id = (uint32_t)(seed * 2654435761u);
    pkg.num_layers = 3u;
    pkg.layer_offsets = offsets;
    pkg.layer_offsets_count = 3u;
    pkg.layers = layers;
    pkg.layers_count = 3u;

    static const char *labels[] = { "base", "delta", "nightly", "release" };

    /* --- layer 0: FEATURES ------------------------------------- */
    layers[0].header.kind = layered_container_meta_LAYER_KIND_FEATURES;
    layers[0].header.version.major = (uint16_t)((seed * 3u) & 0x3FFFu);
    layers[0].header.version.minor = (uint16_t)((seed + 7u) & 0x3FFFu);
    layers[0].header.version.label.bytes = (char *)labels[seed % 4u];
    layers[0].header.version.label.length = strlen(labels[seed % 4u]);
    layers[0].header.lifetime_present = (seed & 1u) != 0u;
    layers[0].header.lifetime = (uint32_t)((seed * 977u) & 0x0FFFFFFFu);
    layered_container_meta_layer_body_set_parameters(
            &layers[0].body, layers[0].header.kind);

    const uint8_t coord_shift = (uint8_t)((seed % 23u) + 9u); /* 9..31 */
    layers[0].body.u.feature_layer.coord_shift = coord_shift;
    static layered_container_feature_feature_record_t records[4];
    memset(records, 0, sizeof(records));
    const size_t n_records = (size_t)((seed % 3u) + 2u); /* 2..4 */
    layers[0].body.u.feature_layer.records = records;
    layers[0].body.u.feature_layer.records_count = n_records;
    static layered_container_feature_geo_point_t path[4][4];
    const uint64_t flags_mask = (((uint64_t)1u << coord_shift) - 1u);
    for (size_t i = 0; i < n_records; ++i) {
        layered_container_feature_feature_record_set_parameters(
                &records[i], coord_shift);
        records[i].feature_id = (uint32_t)((seed * 31u + (unsigned)i * 101u) & 0x0FFFFFFFu);
        records[i].flags = ((uint64_t)seed * 0x9E37u + (uint64_t)i) & flags_mask;
        const size_t n_path = (size_t)(((seed + i) % 3u) + 2u); /* 2..4 */
        records[i].path = path[i];
        records[i].path_count = n_path;
        for (size_t j = 0; j < n_path; ++j) {
            path[i][j].lon = (int32_t)((int)(seed * 13u + (unsigned)(i * 7u + j)) - 2048);
            path[i][j].lat = (int32_t)(1024 - (int)(seed * 11u + (unsigned)(j * 5u)));
        }
    }

    /* --- layer 1: ATTRIBUTES ----------------------------------- */
    layers[1].header.kind = layered_container_meta_LAYER_KIND_ATTRIBUTES;
    layers[1].header.version.major = (uint16_t)((seed + 21u) & 0x3FFFu);
    layers[1].header.version.minor = (uint16_t)((seed * 5u) & 0x3FFFu);
    layers[1].header.version.label.bytes = (char *)labels[(seed + 1u) % 4u];
    layers[1].header.version.label.length = strlen(labels[(seed + 1u) % 4u]);
    layers[1].header.lifetime_present = (seed & 2u) != 0u;
    layers[1].header.lifetime = (uint32_t)((seed * 3301u) & 0x0FFFFFFFu);
    layered_container_meta_layer_body_set_parameters(
            &layers[1].body, layers[1].header.kind);

    static layered_container_meta_attribute_pair_t pairs[4];
    memset(pairs, 0, sizeof(pairs));
    static const char *keys[] = { "speed", "name", "oneway", "surface" };
    static const char *texts[] = { "asphalt", "Main Street", "gravel" };
    const size_t n_pairs = (size_t)((seed % 3u) + 2u); /* 2..4 */
    layers[1].body.u.attributes.pairs = pairs;
    layers[1].body.u.attributes.pairs_count = n_pairs;
    for (size_t i = 0; i < n_pairs; ++i) {
        pairs[i].key.bytes = (char *)keys[i];
        pairs[i].key.length = strlen(keys[i]);
        const unsigned arm = (unsigned)(seed + i) % 3u;
        pairs[i].value.tag = arm;
        switch (arm) {
            case 0u:
                pairs[i].value.u.int_value =
                        (int32_t)((int)(seed * 17u + (unsigned)i) - 4096);
                break;
            case 1u:
                pairs[i].value.u.text_value.bytes = (char *)texts[i % 3u];
                pairs[i].value.u.text_value.length = strlen(texts[i % 3u]);
                break;
            default:
                pairs[i].value.u.flag_value = ((seed + i) & 1u) != 0u;
                break;
        }
    }

    /* --- layer 2: BLOB ------------------------------------------ */
    layers[2].header.kind = layered_container_meta_LAYER_KIND_BLOB;
    layers[2].header.version.major = (uint16_t)((seed + 2u) & 0x3FFFu);
    layers[2].header.version.minor = (uint16_t)((seed * 9u) & 0x3FFFu);
    layers[2].header.version.label.bytes = (char *)labels[(seed + 2u) % 4u];
    layers[2].header.version.label.length = strlen(labels[(seed + 2u) % 4u]);
    layers[2].header.lifetime_present = (seed & 4u) != 0u;
    layers[2].header.lifetime = (uint32_t)((seed * 41u) & 0x0FFFFFFFu);
    layered_container_meta_layer_body_set_parameters(
            &layers[2].body, layers[2].header.kind);

    static uint8_t blob_bytes[4];
    blob_bytes[0] = (uint8_t)(seed & 0xFFu);
    blob_bytes[1] = (uint8_t)((seed >> 8) & 0xFFu);
    blob_bytes[2] = (uint8_t)((seed * 7u) & 0xFFu);
    blob_bytes[3] = (uint8_t)((seed * 13u) & 0xFFu);
    layers[2].body.u.blob.checksum = (uint32_t)((seed * 65599u) & 0x0FFFFFFFu);
    layers[2].body.u.blob.payload.bytes = blob_bytes;
    layers[2].body.u.blob.payload.bit_length = 21u + (seed % 11u); /* 21..31 */

    uint8_t buf[8192];
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, sizeof(buf) * 8u);
    zserio_status_t st = layered_container_layered_container_write(&pkg, &w);
    if (st != ZSERIO_OK) { fprintf(stderr, "write: %d\n", (int)st); return 1; }
    size_t byte_pos = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    FILE *f = fopen(argv[2], "wb"); fwrite(buf, 1, byte_pos, f); fclose(f);
    fprintf(stdout, "c_driver: seed=%u bytes=%zu\n", seed, byte_pos);
    return 0;
}

/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Fabian Klebert
 *
 * Phase 4 conformance entry point: read the user-supplied
 * tile.bin (a SmartLayerTile encoded by the upstream zserio
 * Java/C++ generator + zs-yaml at zserio core 2.18.1) through
 * our generated C99 decoder and surface a few salient fields.
 *
 * Verifies: REQ-FN-001 (byte-exact wire-format read against
 *                        upstream-produced bitstreams).
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zserio/allocator.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>

#include "smart/tile/smart_layer_tile.h"
#include "smart/types/data_layer.h"
#include "smart/types/smart_layer_header.h"
#include "system/types/extern_data.h"
#include "system/types/extern_descriptor.h"
#include "core/types/version_id.h"

/* Per-layer decoders. The externDescriptor.target string identifies
 * which of these to dispatch into; one decoder per target the fixture
 * carries. */
#include "road/layer/road_layer.h"
#include "road/layer/road_geometry_layer.h"
#include "characteristics/layer/road_characteristics_layer.h"
#include "rules/layer/road_rules_layer.h"
#include "road/road/road_list.h"

static int slurp_file(const char *path, uint8_t **buf, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        fprintf(stderr, "open %s: %s\n", path, strerror(errno));
        return -1;
    }
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return -1; }
    long n = ftell(f);
    if (n < 0) { fclose(f); return -1; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return -1; }
    uint8_t *p = (uint8_t *)malloc((size_t)n);
    if (p == NULL) { fclose(f); return -1; }
    if (fread(p, 1, (size_t)n, f) != (size_t)n) {
        free(p);
        fclose(f);
        return -1;
    }
    fclose(f);
    *buf = p;
    *len = (size_t)n;
    return 0;
}

int main(int argc, char **argv)
{
    const char *path = (argc >= 2) ? argv[1] : "conformance/ndslive/tile.bin";

    uint8_t *bytes = NULL;
    size_t len = 0;
    if (slurp_file(path, &bytes, &len) != 0) {
        fprintf(stderr, "ndslive: failed to read %s\n", path);
        return 1;
    }

    zserio_bitstream_reader_t bs;
    zserio_status_t st = zserio_bitstream_reader_init(&bs, bytes, len * 8u);
    if (st != ZSERIO_OK) {
        fprintf(stderr, "ndslive: bitstream init failed: %d\n", (int)st);
        free(bytes);
        return 1;
    }

    zserio_allocator_t alloc = zserio_allocator_libc();

    smart_tile_smart_layer_tile_t tile;
    smart_tile_smart_layer_tile_init(&tile);
    st = smart_tile_smart_layer_tile_read(&tile, &bs, &alloc);
    if (st != ZSERIO_OK) {
        fprintf(stderr, "ndslive: SmartLayerTile read failed: %d\n", (int)st);
        smart_tile_smart_layer_tile_free(&tile, &alloc);
        free(bytes);
        return 1;
    }

    /* Surface enough state to cross-check against tile.yaml without
     * shipping a full reflection facility. The YAML the user provided
     * starts with:
     *   tileId: 545555202
     *   header.numDataLayers: 4
     *   header.availableLayers: [0,1,2,3]
     *   header.layerLifetime[*].lifetime: STATIC (=0)
     *   header.layerLifetime[*].layerLifetimeInfo.versionId: 1
     */
    printf("ndslive: bytes=%zu\n", len);
    printf("ndslive: tileId=%d\n", (int)tile.tile_id);
    printf("ndslive: header.numDataLayers=%u\n",
           (unsigned)tile.header.num_data_layers);
    printf("ndslive: header.availableLayers={");
    for (size_t i = 0; i < (size_t)tile.header.num_data_layers; ++i) {
        printf("%s%u",
               i == 0 ? "" : ",",
               (unsigned)tile.header.available_layers[i]);
    }
    printf("}\n");
    for (size_t i = 0; i < (size_t)tile.header.num_data_layers; ++i) {
        const smart_types_data_layer_lifetime_t *ll =
                &tile.header.layer_lifetime[i];
        printf("ndslive: layer[%zu].lifetime=%u\n",
               i, (unsigned)ll->lifetime);
    }
    printf("ndslive: layers_count=%zu\n", tile.layers_count);
    for (size_t i = 0; i < tile.layers_count; ++i) {
        const smart_types_data_layer_t *dl = &tile.layers[i];
        const system_types_extern_descriptor_t *ed =
                &dl->layer.extern_descriptor;
        printf("ndslive: layer[%zu].externDescriptor name=%.*s version=%.*s target=%.*s\n",
               i,
               (int)ed->name.length, ed->name.bytes ? ed->name.bytes : "",
               (int)ed->version.length, ed->version.bytes ? ed->version.bytes : "",
               (int)ed->target.length, ed->target.bytes ? ed->target.bytes : "");
    }

    /* Dispatch on externDescriptor.target — parse each layer's extern
     * payload into its real typed struct. The extern blob is stored
     * starting at byte 0 / bit 0 of the owned `bytes` buffer
     * (zserio_extern_t materialises bits there), so a bitstream
     * reader over those bytes decodes the layer like any normal
     * compound.
     *
     * KNOWN LIMITATION (Phase 4 follow-up): NDS Live's RoadList /
     * RoadGeometryLayer / RoadCharacteristicsLayer / RoadRulesLayer
     * are `packed Road[]` (or analogues) whose elements are
     * compounds containing varint-typed fields. Upstream encodes
     * each leaf integer with a per-field zserio::DeltaContext, with
     * the delta bit-width observed at init time, plus nested
     * ZserioPackingContext for compound sub-fields. Our generator
     * currently flat-packs only fixed-width integer / bit-field
     * fields. The wire bits still round-trip byte-for-byte (the
     * earlier round-trip assertion proves this), but semantic
     * decoding here hits ZSERIO_ERR_CONSTRAINT_VIOLATED on the
     * Var4ByteId.id varuint32 because we read it as an unpacked
     * varuint, materialising the descriptor bits as part of the
     * value. Listing the dispatch + status here keeps the demo
     * honest about the gap. */
    /* Per-layer trace gating: ZSERIO_TRACE_LAYER=N enables the
     * primitive-read trace exactly during layer N's decode, so the
     * trace file can be diff'd against the cpp oracle's matching
     * trace. The trace itself is written to ZSERIO_TRACE=<path>
     * (handled in runtime/src/bitstream.c). */
    const char *trace_layer_env = getenv("ZSERIO_TRACE_LAYER");
    const long trace_layer = (trace_layer_env != NULL) ? atol(trace_layer_env) : -1L;

    for (size_t i = 0; i < tile.layers_count; ++i) {
        const smart_types_data_layer_t *dl = &tile.layers[i];
        const zserio_string_t *target = &dl->layer.extern_descriptor.target;
        const zserio_extern_t *blob = &dl->layer.data;
        const char *t = target->bytes ? target->bytes : "";
        const size_t tlen = target->length;

        zsc_trace_set_enabled(trace_layer == (long)i);

        zserio_bitstream_reader_t lbs;
        st = zserio_bitstream_reader_init(&lbs, blob->bytes, blob->bit_length);
        if (st != ZSERIO_OK) {
            fprintf(stderr,
                    "ndslive: layer[%zu] sub-stream init failed: %d\n",
                    i, (int)st);
            continue;
        }

        if (tlen == strlen("road.layer.RoadLayer")
                && memcmp(t, "road.layer.RoadLayer", tlen) == 0)
        {
            /* Diagnostic dump: first 32 bytes of the extern blob to
             * cross-check against upstream's wire layout when chasing
             * decoder regressions. */
            printf("ndslive: layer[0] extern blob (first 32 bytes):");
            const size_t dump = (blob->bit_length / 8u) < 32u
                    ? (blob->bit_length / 8u) : 32u;
            for (size_t b = 0u; b < dump; ++b) {
                printf(" %02x", (unsigned)blob->bytes[b]);
            }
            printf("\n");

            road_layer_road_layer_t rl;
            road_layer_road_layer_init(&rl);
            st = road_layer_road_layer_read(&rl, &lbs, &alloc);
            printf("ndslive: layer[%zu] RoadLayer dispatch: status=%d%s\n",
                   i, (int)st,
                   st == ZSERIO_OK ? " (decoded)" : " (gap: nested packed varint)");
            if (st == ZSERIO_OK) {
                printf("ndslive: layer[0] coord_shift=%u roads_count=%zu\n",
                       (unsigned)rl.coord_shift, rl.road_list.roads_count);
            }
            road_layer_road_layer_free(&rl, &alloc);
        }
        else if (tlen == strlen("road.layer.RoadGeometryLayer")
                && memcmp(t, "road.layer.RoadGeometryLayer", tlen) == 0)
        {
            road_layer_road_geometry_layer_t gl;
            road_layer_road_geometry_layer_init(&gl);
            st = road_layer_road_geometry_layer_read(&gl, &lbs, &alloc);
            printf("ndslive: layer[%zu] RoadGeometryLayer dispatch: status=%d%s\n",
                   i, (int)st,
                   st == ZSERIO_OK ? " (decoded)" : " (gap: nested packed varint)");
            road_layer_road_geometry_layer_free(&gl, &alloc);
        }
        else if (tlen == strlen("characteristics.layer.RoadCharacteristicsLayer")
                && memcmp(t, "characteristics.layer.RoadCharacteristicsLayer", tlen) == 0)
        {
            characteristics_layer_road_characteristics_layer_t cl;
            characteristics_layer_road_characteristics_layer_init(&cl);
            st = characteristics_layer_road_characteristics_layer_read(
                    &cl, &lbs, &alloc);
            printf("ndslive: layer[%zu] RoadCharacteristicsLayer dispatch: "
                   "status=%d%s\n",
                   i, (int)st,
                   st == ZSERIO_OK ? " (decoded)" : " (gap: nested packed varint)");
            characteristics_layer_road_characteristics_layer_free(&cl, &alloc);
        }
        else if (tlen == strlen("rules.layer.RoadRulesLayer")
                && memcmp(t, "rules.layer.RoadRulesLayer", tlen) == 0)
        {
            rules_layer_road_rules_layer_t rl;
            rules_layer_road_rules_layer_init(&rl);
            st = rules_layer_road_rules_layer_read(&rl, &lbs, &alloc);
            printf("ndslive: layer[%zu] RoadRulesLayer dispatch: status=%d%s\n",
                   i, (int)st,
                   st == ZSERIO_OK ? " (decoded)" : " (gap: nested packed varint)");
            rules_layer_road_rules_layer_free(&rl, &alloc);
        }
        else
        {
            printf("ndslive: layer[%zu] unrecognized target — skipping\n", i);
        }
        zsc_trace_set_enabled(false);
    }

    /* Round-trip: write the in-memory tile back through the generated
     * encoder and assert the resulting bytes are byte-identical to the
     * input. This catches write-side gaps that the read-only path
     * cannot — alignment-write padding, packed-array delta writes,
     * varint write boundaries, choice / union dispatch on the write
     * path. (Verifies REQ-FN-001 in both directions for this fixture.) */
    {
        const size_t capacity_bits = len * 8u;
        uint8_t *out_buf = (uint8_t *)calloc(len, 1u);
        if (out_buf == NULL) {
            fprintf(stderr, "ndslive: round-trip alloc failed\n");
            smart_tile_smart_layer_tile_free(&tile, &alloc);
            free(bytes);
            return 1;
        }
        zserio_bitstream_writer_t bw;
        st = zserio_bitstream_writer_init(&bw, out_buf, capacity_bits);
        if (st != ZSERIO_OK) {
            fprintf(stderr, "ndslive: writer init failed: %d\n", (int)st);
            free(out_buf);
            smart_tile_smart_layer_tile_free(&tile, &alloc);
            free(bytes);
            return 1;
        }
        st = smart_tile_smart_layer_tile_write(&tile, &bw);
        if (st != ZSERIO_OK) {
            fprintf(stderr, "ndslive: SmartLayerTile write failed: %d\n",
                    (int)st);
            free(out_buf);
            smart_tile_smart_layer_tile_free(&tile, &alloc);
            free(bytes);
            return 1;
        }
        const size_t written_bits = bw.bit_index;
        const size_t written_bytes = (written_bits + 7u) / 8u;
        printf("ndslive: round-trip wrote %zu bits (%zu bytes)\n",
               written_bits, written_bytes);
        if (written_bytes != len) {
            fprintf(stderr,
                    "ndslive: round-trip size mismatch: in=%zu out=%zu\n",
                    len, written_bytes);
            free(out_buf);
            smart_tile_smart_layer_tile_free(&tile, &alloc);
            free(bytes);
            return 1;
        }
        if (memcmp(out_buf, bytes, len) != 0) {
            size_t diff_at = 0;
            while (diff_at < len && out_buf[diff_at] == bytes[diff_at]) {
                ++diff_at;
            }
            fprintf(stderr,
                    "ndslive: round-trip MISMATCH at byte %zu "
                    "(in=0x%02x, out=0x%02x)\n",
                    diff_at,
                    (unsigned)bytes[diff_at],
                    (unsigned)out_buf[diff_at]);
            free(out_buf);
            smart_tile_smart_layer_tile_free(&tile, &alloc);
            free(bytes);
            return 1;
        }
        printf("ndslive: round-trip byte-equal OK (REQ-FN-001)\n");
        free(out_buf);
    }

    smart_tile_smart_layer_tile_free(&tile, &alloc);
    free(bytes);
    printf("ndslive: read OK\n");
    return 0;
}

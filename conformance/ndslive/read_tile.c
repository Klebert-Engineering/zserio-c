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

    smart_tile_smart_layer_tile_free(&tile, &alloc);
    free(bytes);
    printf("ndslive: read OK\n");
    return 0;
}

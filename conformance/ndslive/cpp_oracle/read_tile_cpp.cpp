// zserio-c oracle driver: reads tile.bin via upstream's C++ runtime.
//
// Enables the BitStreamReader trace (set ZSERIO_TRACE=<path>) so each
// primitive read records one line:
//   <pos_after> read <nbits> <value>
// To focus the trace on a specific layer, pass `--layer <0..3>`:
// SmartLayerTile is decoded with tracing disabled, then layer N's
// extern payload is re-opened as a fresh BitStreamReader and decoded
// as the appropriate layer type with tracing enabled. The resulting
// trace can be diff'd against our C reader's equivalent trace
// (build-conformance/read_tile with ZSERIO_TRACE_LAYER=N) to localise
// wire-format divergence to a specific bit position.

#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "zserio/BitStreamReader.h"
#include "zserio/BitBuffer.h"
#include "nds/smart/tile/SmartLayerTile.h"
#include "nds/road/layer/RoadLayer.h"
#include "nds/road/layer/RoadGeometryLayer.h"
#include "nds/characteristics/layer/RoadCharacteristicsLayer.h"
#include "nds/rules/layer/RoadRulesLayer.h"

extern "C" void zsc_trace_set_enabled(bool enabled);

int main(int argc, char *argv[])
{
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <tile.bin> [--layer N]\n", argv[0]);
        return 1;
    }
    int layerIdx = -1;
    for (int i = 2; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--layer") == 0) {
            layerIdx = std::atoi(argv[i + 1]);
        }
    }

    std::FILE *f = std::fopen(argv[1], "rb");
    if (!f) { std::perror("fopen"); return 1; }
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> buf(static_cast<size_t>(n));
    if (std::fread(buf.data(), 1, buf.size(), f) != buf.size()) {
        std::fprintf(stderr, "short read\n");
        return 1;
    }
    std::fclose(f);

    // The SmartLayerTile decode itself produces only ~67 trace lines —
    // layer payloads are opaque ExternData blobs at this level. So
    // tracing here would be useless. The interesting bits are inside
    // each layer's own decode.
    zsc_trace_set_enabled(false);
    ::zserio::BitStreamReader reader(buf.data(), buf.size());
    ::nds::smart::tile::SmartLayerTile tile(reader);
    const auto &layers = tile.getLayers();
    std::fprintf(stdout, "cpp: tile decoded, layers=%zu\n", layers.size());

    if (layerIdx < 0 || layerIdx >= static_cast<int>(layers.size())) {
        std::fprintf(stdout, "cpp: no --layer specified, exiting after tile decode\n");
        return 0;
    }

    const auto &layer = layers[layerIdx].getLayer();
    const auto &target = layer.getExternDescriptor().getTarget();
    std::string targetStr(reinterpret_cast<const char*>(target.data()), target.size());
    std::fprintf(stdout, "cpp: layer[%d] target=%s\n", layerIdx, targetStr.c_str());

    const ::zserio::BitBuffer &blob = layer.getData();
    ::zserio::BitStreamReader layerReader(blob.getBuffer(), blob.getBitSize(),
            ::zserio::BitsTag());

    zsc_trace_set_enabled(true);

    try {
        if (targetStr == "road.layer.RoadLayer") {
            ::nds::road::layer::RoadLayer dec(layerReader);
            std::fprintf(stdout, "cpp: layer[%d] RoadLayer decoded, finalBitPos=%zu\n",
                         layerIdx, layerReader.getBitPosition());
        } else if (targetStr == "road.layer.RoadGeometryLayer") {
            ::nds::road::layer::RoadGeometryLayer dec(layerReader);
            std::fprintf(stdout, "cpp: layer[%d] RoadGeometryLayer decoded, finalBitPos=%zu\n",
                         layerIdx, layerReader.getBitPosition());
        } else if (targetStr == "characteristics.layer.RoadCharacteristicsLayer") {
            ::nds::characteristics::layer::RoadCharacteristicsLayer dec(layerReader);
            std::fprintf(stdout, "cpp: layer[%d] RoadCharacteristicsLayer decoded, finalBitPos=%zu\n",
                         layerIdx, layerReader.getBitPosition());
        } else if (targetStr == "rules.layer.RoadRulesLayer") {
            ::nds::rules::layer::RoadRulesLayer dec(layerReader);
            std::fprintf(stdout, "cpp: layer[%d] RoadRulesLayer decoded, finalBitPos=%zu\n",
                         layerIdx, layerReader.getBitPosition());
        } else {
            std::fprintf(stdout, "cpp: layer[%d] unknown target %s\n",
                         layerIdx, targetStr.c_str());
            return 3;
        }
    } catch (const std::exception &e) {
        std::fprintf(stdout, "cpp: layer[%d] decode failed: %s, finalBitPos=%zu\n",
                     layerIdx, e.what(), layerReader.getBitPosition());
        return 2;
    }
    return 0;
}

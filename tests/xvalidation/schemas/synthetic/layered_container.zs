/*
 * Synthetic production-shape schema. Mirrors the complexity profile
 * of a real layered map-tile payload — multi-package layout, indexed
 * offsets into variable-length layers, an enum-dispatched choice per
 * layer, parameterized compounds with dynamic bitfields, packed
 * compound arrays, strings, unions, an extern blob, and alignment —
 * without deriving from any proprietary schema.
 */
package layered_container;

import layered_container.core.*;
import layered_container.meta.*;

struct LayerHeader
{
    LayerKind          kind;
    VersionInfo        version;
    optional varuint32 lifetime;
};

struct Layer
{
    LayerHeader header;
    align(32):
    LayerBody(header.kind) body;
};

struct LayeredContainer
{
    uint32  containerId;
    varsize numLayers;
    uint32  layerOffsets[numLayers];
layerOffsets[@index]:
    Layer   layers[numLayers];
};

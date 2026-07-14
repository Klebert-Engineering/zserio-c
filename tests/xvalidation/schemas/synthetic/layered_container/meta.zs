package layered_container.meta;

import layered_container.core.*;
import layered_container.feature.*;

enum uint8 LayerKind
{
    FEATURES,
    ATTRIBUTES,
    BLOB
};

union AttributeValue
{
    varint32 intValue;
    string   textValue;
    bool     flagValue;
};

struct AttributePair
{
    string         key;
    AttributeValue value;
};

struct AttributeSet
{
    packed AttributePair pairs[];
};

choice LayerBody(LayerKind kind) on kind
{
    case FEATURES:
        FeatureLayer featureLayer;
    case ATTRIBUTES:
        AttributeSet attributes;
    case BLOB:
        BlobInfo blob;
};

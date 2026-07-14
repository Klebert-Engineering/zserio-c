package layered_container.feature;

struct GeoPoint
{
    varint32 lon;
    varint32 lat;
};

struct FeatureRecord(uint8 coordShift)
{
    varuint32       featureId;
    bit<coordShift> flags;
    packed GeoPoint path[];
};

struct FeatureLayer
{
    uint8 coordShift : coordShift > 0 && coordShift <= 31;
    FeatureRecord(coordShift) records[];
};

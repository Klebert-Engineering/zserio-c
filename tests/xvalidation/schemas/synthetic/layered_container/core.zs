package layered_container.core;

struct VersionInfo
{
    varuint16 major;
    varuint16 minor;
    string    label;
};

struct BlobInfo
{
    varuint32 checksum;
    extern    payload;
};

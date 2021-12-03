#include <cstdint>
#include <unordered_map>
#include <vector>

constexpr uint32_t RPAK_MAGIC       = 0x6b615052;
constexpr uint32_t RPAK_VERSION     = 8;
constexpr size_t   RPAK_HEADER_SIZE = 0x80;

constexpr uint32_t RPAK_TXTR = 'rtxt';
constexpr uint32_t RPAK_MATL = 'ltam';

struct descriptor_t {
    uint32_t page;
    uint32_t offset;
};

union descriptor_u {
    uint8_t*     ptr;
    descriptor_t desc;
};

struct rfile_t {
    uint64_t guid;
    uint64_t _name_pad;

    descriptor_u description;
    descriptor_u data;

    uint64_t starpak;
    uint64_t starpak_opt;

    uint16_t _unk30;
    uint16_t _unk32;

    uint32_t _unk34;
    uint32_t _start_idx;
    uint32_t _unk3c;
    uint32_t _count;

    uint32_t desc_size;
    uint32_t desc_align;

    uint32_t ext;
};
static_assert(sizeof(rfile_t) == 0x50);

struct rpak_header_t {
    uint32_t magic;
    uint16_t version;
    uint16_t flags;

    uint64_t timestamp;

    uint64_t unk10;
    uint64_t size_disk;

    uint64_t unk20;
    uint64_t unk28;

    uint64_t size_decompressed;
    uint64_t unk38;
    uint64_t unk40;

    uint16_t starpak_len;
    uint16_t starpak_opt_len;
    uint16_t sections_num;
    uint16_t data_chunks_num;

    uint16_t patches_num;
    uint16_t unk52;

    uint32_t unk54; // num descriptors
    uint32_t num_files;
    uint32_t relationship; // mislabeled as 'relationship'

    uint32_t unk60;
    uint32_t unk64;
    uint32_t unk68;
    uint32_t unk6c;

    uint32_t unk70;
    uint32_t unk74;

    uint32_t unk78;
};
static_assert(sizeof(rpak_header_t) == RPAK_HEADER_SIZE);

struct matl_t {
    uint64_t _pad0;
    uint64_t _pad8;

    uint64_t guid;

    const char* name;
    const char* mat_name; // metal, flesh, glass...

    uint8_t pad_0020[64]; //0x0020

    uint64_t* guids;
    uint64_t* guids_end;
};

#pragma pack(push, 1)
struct txtr_t {
    uint64_t guid;
    uint64_t _name_pad;

    uint16_t width;
    uint16_t height;

    uint16_t _unk14;

    uint16_t texture_type;

    uint32_t total_size; // I won't ever check this lmfao
    uint8_t  _unk1c;

    uint8_t starpak_opt_mipmaps_num;

    uint8_t layers_count;
    uint8_t _unk19;
    uint8_t _unk20;

    uint8_t rpak_mipmaps_num;
    uint8_t starpak_mipmaps_num;
};
//static_assert goes here, not really needed since we always "seek" to that desc.
#pragma pack(pop)

// struct texture_t {
//     txtr_t* desc;
//     void*   data;
// };

class RPak {
public:
    // decompressed data...
    RPak(uint8_t* deta);

    std::unordered_map<uint64_t, rfile_t>    files;
    std::unordered_map<std::string, matl_t*> materials; // since I'm narrow minded...
    // std::unordered_map<uint64_t, texture_t> textures;
};
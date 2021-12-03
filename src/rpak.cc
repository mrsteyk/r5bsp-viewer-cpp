#include "rpak.hh"

#include <iostream>

struct data_chunks_t {
    uint32_t section_id;
    uint32_t align_byte;
    uint32_t size;
};
static_assert(sizeof(data_chunks_t) == 12);

RPak::RPak(uint8_t* deta) {
    rpak_header_t* header = reinterpret_cast<rpak_header_t*>(deta);
    auto           data   = deta + 0x80;

    // if (header->unk74 != 0) {
    //     this->succ = false;
    //     return;
    // }

    auto starpak_start = data + header->starpak_len;

    auto starpak_skipped = starpak_start + header->starpak_opt_len;
    auto sections        = starpak_skipped; // TODO: cast

    auto sections_skipped = starpak_skipped + (16ull * header->sections_num);
    auto data_chunks      = reinterpret_cast<data_chunks_t*>(sections_skipped); // TODO: cast

    auto data_chunks_skipped = sections_skipped + (12ull * header->data_chunks_num);
    auto descriptors         = reinterpret_cast<descriptor_t*>(data_chunks_skipped); // TODO: cast

    auto unk54_skipped = data_chunks_skipped + (8ull * header->unk54);
    // parsing of files is moved for pointer conversion...

    auto file_entries_skipped = unk54_skipped + (0x50ull * header->num_files);
    // unk5c (8)

    auto unk5c_skipped = file_entries_skipped + (8ull * header->relationship);

    auto unk60_skipped = unk5c_skipped + (4ull * header->unk60);

    auto unk64_skipped = unk60_skipped + (4ull * header->unk64);

    auto unk68_skipped = unk64_skipped + (1ull * header->unk68);

    auto unk6c_skipped = unk68_skipped + (16ull * header->unk6c);

    auto unk70_skipped = unk6c_skipped + (24ull * header->unk70);

    // pages start here
    std::vector<uint8_t*> pages(header->data_chunks_num);
    pages[0] = unk70_skipped;
    for (size_t i = 1; i < pages.size(); i++) {
        pages[i] = pages[i - 1] + data_chunks[i - 1].size;
    }

    // pointer parsing...
    for (size_t i = 0; i < header->unk54; i++) {
        const auto& d = descriptors[i];

        auto desc = reinterpret_cast<descriptor_u*>(pages[d.page] + d.offset);
        desc->ptr = pages[desc->desc.page] + desc->desc.offset;
    }

    auto files = reinterpret_cast<rfile_t*>(unk54_skipped);
    // this looks janky af
    for (size_t i = 0; i < header->num_files; i++) {
        auto& file           = files[i];
        file.description.ptr = pages[file.description.desc.page] + file.description.desc.offset;
        if (file.data.desc.page != std::numeric_limits<uint32_t>::max())
            file.data.ptr = pages[file.data.desc.page] + file.data.desc.offset;
        else
            file.data.ptr = 0; // ez checks

        this->files[file.guid] = file;

        if (file.ext == RPAK_MATL) {
            auto d                = reinterpret_cast<matl_t*>(file.description.ptr);
            auto name             = std::string(d->name);
            this->materials[name] = d;

            // std::cout << name << std::endl;
        }
    }
}
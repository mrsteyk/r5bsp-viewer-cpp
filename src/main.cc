#include "static.hh"

#include "pfd.hh"

#include <glad/glad.h>

#include <GLFW/glfw3.h>

#include "imgui/imgui.h"

#include "imgui/backends/imgui_impl_glfw.h"
#include "imgui/backends/imgui_impl_opengl3.h"

#include <glm/ext/matrix_clip_space.hpp> // glm::perspective
#include <glm/ext/matrix_transform.hpp> // glm::translate, glm::rotate, glm::scale
#include <glm/ext/scalar_constants.hpp> // glm::pi
#include <glm/gtx/euler_angles.hpp>
#include <glm/mat4x4.hpp> // glm::mat4

#include <algorithm>
#include <fstream>
#include <locale>
#include <string>
#include <utility>
#include <vector>

#include "decomp.hh"
#include "rpak.hh"

constexpr int DEFAULT_W = 1280;
constexpr int DEFAULT_H = 720;

struct pipeline_t {
    GLuint program;
    GLuint pipeline;
};

// dac wasn't used because this game is based on indicies
// struct dac_t final {
//     uint32_t vertices;
//     uint32_t instances = 1; // I never set this...
//     uint32_t base_vertex;
//     uint32_t base_instance = 0; // I never set this...
// };

struct dec_t final {
    uint32_t indices; // count
    uint32_t instances = 1; // I never set this...
    uint32_t base_index;
    uint32_t base_vertex;
    uint32_t base_instance = 0; // I never set this...
};

#pragma pack(push, 1)
enum class LUMPS : uint32_t {
    TEXTURE_DATA  = 0x2,
    MODELS        = 0xE,
    SURFACE_NAMES = 0xF,

    MESHES        = 0x50,
    MATERIAL_SORT = 0x52,

    VERTEX         = 0x3,
    VERTEX_NORMALS = 0x1E,
    PACKED_VERTEX  = 0x14,
    MESH_INDICIES  = 0x4F,

    VERTEX_BLINN_PHONG = 0x4B,
    VERTEX_LIT_BUMP    = 0x49,
    VERTEX_LIT_FLAT    = 0x48,
    VERTEX_UNLIT       = 0x47,
    VERTEX_UNLIT_TS    = 0x4A,
};

enum class VERTEX_FLAGS : uint32_t {
    // Flags for future colour rendering???
    SKY_2D  = 0x00002,
    SKY     = 0x00004,
    TRIGGER = 0x40000,

    VERTEX_LIT_FLAT = 0x000,
    VERTEX_LIT_BUMP = 0x200,
    VERTEX_UNLIT    = 0x400,
    VERTEX_UNLIT_TS = 0x600,

    MASK = 0x600,
};

struct lump_t final {
    uint32_t offset;
    uint32_t size;
    uint32_t version;
    uint32_t cc;
};

struct bsp_header_t final {
    uint32_t header; // 0x50534272
    uint32_t version; // 47-50?
    uint32_t map_version;
    uint32_t unkC; // 0x7F
    lump_t   lumps[0x7F];
};

// 0x2
struct texture_data_t final {
    uint32_t name_index;
    uint32_t texture_width;
    uint32_t texture_height;
    uint32_t flags;
};

// 0x3
union vertex_t final {
    struct {
        float x;
        float y;
        float z;
    };
    float coords[3];
};

// 0x14
union packed_vertex_t final {
    struct {
        int16_t x;
        int16_t y;
        int16_t z;
    };
    int16_t coords[3];
};

// 0x50
struct mesh_t final {
    uint32_t first_mesh_index;
    uint16_t num_triangles;

    uint16_t _pad; // ???

    int32_t unk[3];
    int16_t unk1;

    uint16_t material_sort;
    uint32_t flags;
};
static_assert(sizeof(mesh_t) == 28);

using mesh_index = uint16_t;

// 0xE
struct model_t final {
    float mins[3];
    float maxs[3];

    uint32_t first_mesh;
    uint32_t num_meshes;

    int32_t unk[8];
};

struct material_sort_t final {
    uint16_t texture_data;
    uint16_t lightmap_idx;

    uint16_t unk[2];

    uint32_t vertex_offset;
};
static_assert(sizeof(material_sort_t) == 12);

// --- Vertex lumps, I render them all as one lmfao
// TODO: seperate shaders for different types? maybe colours for now?

// 0x4B
struct vertex_blinn_phong_t final {
    uint32_t pos_index;
    uint32_t nrm_index;
    float    uv[2];
    float    uv2[2];
};
static_assert(sizeof(vertex_blinn_phong_t) == 24);

// 0x49
struct vertex_lit_bump_t final {
    uint32_t pos_index;
    uint32_t nrm_index;
    float    uv[2];

    int32_t unused;

    float unk[3];
};
static_assert(sizeof(vertex_lit_bump_t) == 32);

// 0x48
struct vertex_lit_flat_t final {
    uint32_t pos_index;
    uint32_t nrm_index;
    float    uv[2];

    int32_t unk;
};
static_assert(sizeof(vertex_lit_flat_t) == 20);

// 0x47
// struct vertex_unlit final {
//     uint32_t pos_index;
//     uint32_t nrm_index;
//     float    uv[2];
// }
typedef vertex_lit_flat_t vertex_unlit_t;
static_assert(sizeof(vertex_unlit_t) == 20);

// 0x4A
struct vertex_unlit_ts_t final {
    uint32_t pos_index;
    uint32_t nrm_index;
    float    uv[2];

    uint64_t unk; // 8 bytes
};
static_assert(sizeof(vertex_unlit_ts_t) == 24);
#pragma pack(pop)

struct mesh_parsed_t {
    dec_t        dec; // move to implement MDI?
    GLuint       dec_buf; // buffer associated with the dec
    VERTEX_FLAGS flag;
    GLuint       texture;
    bool         textured = false;
    bool         draw     = true;
};

struct model_parsed_t {
    std::vector<mesh_parsed_t> meshes;
};

struct stk_vertex_t {
    vertex_t pos;
    vertex_t normal;
    float    uv[2];
    // bool     texture = false;
};

struct texture_t {
    std::string material_name;
    GLuint      texture;
};

struct stk_map_t {
    GLuint gl_vertex_array;

    GLuint                index_buffer;
    std::vector<uint32_t> index_vec;

    GLuint                    vertex_buffer;
    std::vector<stk_vertex_t> vertex_vec;

    std::vector<model_parsed_t> models;

    std::unordered_map<std::string, texture_t> textures;

    bool loaded = false;
};

struct {
    RPak*                common = nullptr;
    std::vector<uint8_t> common_data;

    RPak*                common_mp = nullptr;
    std::vector<uint8_t> common_mp_data;

    RPak*                common_early = nullptr;
    std::vector<uint8_t> common_early_data;

    RPak*                map = nullptr;
    std::vector<uint8_t> map_data;

    GLuint sampler;
    GLuint error_texture;
} rpaks;

template <typename T>
inline std::vector<T> read_lump(std::ifstream& file, bsp_header_t& header, LUMPS lump) {
    const auto seekdir = std::ios_base::_Seekdir::_Seekbeg;

    std::vector<T> ret(header.lumps[(size_t)lump].size / sizeof(T));
    file.seekg(header.lumps[(size_t)lump].offset, seekdir);
    file.read((char*)ret.data(), ret.size() * sizeof(T));

    return ret;
}

std::pair<bool, stk_map_t> load_map(std::ifstream& file) {
    bsp_header_t header;
    file.read(reinterpret_cast<char*>(&header), sizeof(header));

    auto texture_data  = read_lump<texture_data_t>(file, header, LUMPS::TEXTURE_DATA);
    auto surface_names = read_lump<char>(file, header, LUMPS::SURFACE_NAMES);

    auto models = read_lump<model_t>(file, header, LUMPS::MODELS);
    // for (const auto& model : models) {
    //     std::printf("model %d [%d]\n", model.first_mesh, model.num_meshes);
    // }
    auto meshes = read_lump<mesh_t>(file, header, LUMPS::MESHES);
    // for (const auto& mesh : meshes) {
    //     std::printf("mesh %d [%d] %X\n", mesh.first_mesh_index, mesh.num_triangles, mesh.flags & 0x600);
    // }
    auto material_sorts = read_lump<material_sort_t>(file, header, LUMPS::MATERIAL_SORT);

    auto mesh_indicies = read_lump<mesh_index>(file, header, LUMPS::MESH_INDICIES);

    auto vertex_unlit    = read_lump<vertex_unlit_t>(file, header, LUMPS::VERTEX_UNLIT);
    auto vertex_lit_flat = read_lump<vertex_lit_flat_t>(file, header, LUMPS::VERTEX_LIT_FLAT);
    auto vertex_lit_bump = read_lump<vertex_lit_bump_t>(file, header, LUMPS::VERTEX_LIT_BUMP);
    auto vertex_unlit_ts = read_lump<vertex_unlit_ts_t>(file, header, LUMPS::VERTEX_UNLIT_TS);
    //auto vertex_blinn_phong = read_lump<vertex_unlit_t>(file, header, LUMPS::VERTEX_BLINN_PHONG); // unused???

    auto vertex         = read_lump<vertex_t>(file, header, LUMPS::VERTEX);
    auto vertex_normals = read_lump<vertex_t>(file, header, LUMPS::VERTEX_NORMALS);

    // combine into one buffer???
    // Start is needed when converting into big buffer indicies?
    size_t vertex_unlit_start    = 0;
    auto   vertex_lit_flat_start = vertex_unlit_start + vertex_unlit.size();
    auto   vertex_lit_bump_start = vertex_lit_flat_start + vertex_lit_flat.size();
    auto   vertex_unlit_ts_start = vertex_lit_bump_start + vertex_lit_bump.size();
    auto   total_size            = vertex_unlit_ts_start + vertex_unlit_ts.size();
    std::printf("[%llu]: %llu %llu %llu %llu\n", total_size, vertex_unlit_start, vertex_lit_flat_start, vertex_lit_bump_start, vertex_unlit_ts_start);

    // I feel like packin' today...
    // Wait I can't pack... or can I???
    // This is legit dumb...
    std::vector<stk_vertex_t> vertex_big(total_size);
    for (size_t i = 0; i < vertex_unlit.size(); i++) {
        const auto&  vert = vertex_unlit[i];
        stk_vertex_t tmp_unlit;
        tmp_unlit.pos                      = vertex[vert.pos_index];
        tmp_unlit.normal                   = vertex_normals[vert.nrm_index];
        tmp_unlit.uv[0]                    = vert.uv[0];
        tmp_unlit.uv[2]                    = vert.uv[1];
        vertex_big[vertex_unlit_start + i] = tmp_unlit;
    }
    for (size_t i = 0; i < vertex_lit_flat.size(); i++) {
        const auto&  vert = vertex_lit_flat[i];
        stk_vertex_t tmp_lit_flat;
        tmp_lit_flat.pos                      = vertex[vert.pos_index];
        tmp_lit_flat.normal                   = vertex_normals[vert.nrm_index];
        tmp_lit_flat.uv[0]                    = vert.uv[0];
        tmp_lit_flat.uv[2]                    = vert.uv[1];
        vertex_big[vertex_lit_flat_start + i] = tmp_lit_flat;
    }
    for (size_t i = 0; i < vertex_lit_bump.size(); i++) {
        const auto&  vert = vertex_lit_bump[i];
        stk_vertex_t tmp_lit_bump;
        tmp_lit_bump.pos                      = vertex[vert.pos_index];
        tmp_lit_bump.normal                   = vertex_normals[vert.nrm_index];
        tmp_lit_bump.uv[0]                    = vert.uv[0];
        tmp_lit_bump.uv[2]                    = vert.uv[1];
        vertex_big[vertex_lit_bump_start + i] = tmp_lit_bump;
    }
    for (size_t i = 0; i < vertex_unlit_ts.size(); i++) {
        const auto&  vert = vertex_unlit_ts[i];
        stk_vertex_t tmp_unlit_ts;
        tmp_unlit_ts.pos                      = vertex[vert.pos_index];
        tmp_unlit_ts.normal                   = vertex_normals[vert.nrm_index];
        tmp_unlit_ts.uv[0]                    = vert.uv[0];
        tmp_unlit_ts.uv[2]                    = vert.uv[1];
        vertex_big[vertex_unlit_ts_start + i] = tmp_unlit_ts;
    }

    stk_map_t stk_map;
    stk_map.vertex_vec = std::move(vertex_big);
    stk_map.index_vec  = std::vector<uint32_t>(mesh_indicies.begin(), mesh_indicies.end());

    std::vector<model_parsed_t> models_parsed;
    models_parsed.reserve(models.size());
    for (const auto& model : models) {
        std::vector<mesh_parsed_t> meshes_parsed(model.num_meshes);
        for (size_t mesh_idx = model.first_mesh; mesh_idx < (model.first_mesh + model.num_meshes); mesh_idx++) {
            const auto& mesh          = meshes[mesh_idx];
            const auto& material_sort = material_sorts[mesh.material_sort];

            const auto start  = mesh.first_mesh_index;
            const auto finish = start + (mesh.num_triangles * 3);

            const auto  texture_data_index = material_sort.texture_data;
            const auto& texture_data_elem  = texture_data[texture_data_index];
            const auto  surface_name_raw   = surface_names.data() + texture_data_elem.name_index;
            const auto  surface_name_std   = std::string(surface_name_raw);
            auto        surface_name       = std::string(surface_name_std);

            std::transform(surface_name_std.begin(), surface_name_std.end(), surface_name.begin(), [](char c) {if (c == '\\') return (int)'/'; else return ::tolower(c); });

            const auto  type             = mesh.flags & uint32_t(VERTEX_FLAGS::MASK);
            const char* type_string      = "";
            size_t      additional_start = 0;
            switch (VERTEX_FLAGS(type)) {
            case VERTEX_FLAGS::VERTEX_UNLIT:
                type_string      = "unlit";
                additional_start = vertex_unlit_start;
                break;
            case VERTEX_FLAGS::VERTEX_UNLIT_TS:
                type_string      = "unlit_ts";
                additional_start = vertex_unlit_ts_start;
                break;
            case VERTEX_FLAGS::VERTEX_LIT_FLAT:
                type_string      = "lit_flat";
                additional_start = vertex_lit_flat_start;
                break;
            case VERTEX_FLAGS::VERTEX_LIT_BUMP:
                type_string      = "lit_bump";
                additional_start = vertex_lit_bump_start;
                break;
            default:
                std::cerr << "WHAT???" << std::endl;
                return {false, stk_map_t{}};
            }

            // const auto base_vertex = material_sort.vertex_offset;
            // const auto indicies
            dec_t dec;
            dec.indices     = mesh.num_triangles * 3;
            dec.base_index  = start;
            dec.base_vertex = additional_start + material_sort.vertex_offset;
            // base_index = vertex_unlit + vertex_lit_flat + vertex_lit_bump + vertex_unlit_ts
            // base_vertex = NEEDED_BUFFER_START + vertex_offset
            // index = base_vertex + base_index[i] = NEEDED_BUFFER_START + vertex_offset + base_index[i] = vertex_offset + NEEDED_BUFFER[i]
            // vertex = indicies[index]

            mesh_parsed_t mp;
            mp.dec  = dec;
            mp.flag = VERTEX_FLAGS(type);

            auto texture_map_elem = stk_map.textures.find(surface_name);
            if (texture_map_elem == stk_map.textures.end()) {
                bool found = false;
                auto elem  = rpaks.common_early->materials.find(surface_name);
                auto rpak  = rpaks.common_early;
                if (rpaks.common_early && (elem != rpaks.common_early->materials.end())) {
                    found = true;
                    std::cout << "COMMON EARLY ";
                } else if (rpaks.common && ((elem = rpaks.common->materials.find(surface_name)) != rpaks.common->materials.end())) {
                    found = true;
                    rpak  = rpaks.common;
                    std::cout << "COMMON ";
                } else if (rpaks.common_mp && ((elem = rpaks.common_mp->materials.find(surface_name)) != rpaks.common_mp->materials.end())) {
                    found = true;
                    rpak  = rpaks.common_mp;
                    std::cout << "COMMON MP ";
                } else if (rpaks.map && ((elem = rpaks.map->materials.find(surface_name)) != rpaks.map->materials.end())) {
                    found = true;
                    rpak  = rpaks.map;
                    std::cout << "MAP ";
                }
                std::cout << surface_name << ' '; // << std::endl;

                if (found) {
                    auto guids  = elem->second ? *(uint64_t**)(uintptr_t(elem->second) + 0x60) : nullptr;
                    auto albedo = guids ? guids[0] : 0;
                    if (!albedo) {
                        std::cout << guids ? "NO_ALBEDO " : "FUCK ";
                    } else if (rpak->files.find(albedo) != rpak->files.end()) {
                        const auto& file          = rpak->files[albedo];
                        auto        txtr          = (txtr_t*)file.description.ptr;
                        auto        data          = file.data.ptr;
                        auto        total_mipmaps = +txtr->rpak_mipmaps_num + txtr->starpak_opt_mipmaps_num + txtr->starpak_mipmaps_num; // - 1; // mb -1, mb not?

                        auto rpak_width  = std::max(txtr->width >> total_mipmaps, 4);
                        auto rpak_height = std::max(txtr->height >> total_mipmaps, 4);
                        std::cout << txtr->texture_type << ' ' << txtr->width << 'x' << txtr->height << ' ' << rpak_width << 'x' << rpak_height << ' ' << total_mipmaps << ' ';
                        std::cout << ' ' << +txtr->starpak_opt_mipmaps_num << ' ' << +txtr->starpak_mipmaps_num << ' ' << +txtr->rpak_mipmaps_num << ' ';

                        GLsizei rhsz = 0;

                        auto   format = GL_COMPRESSED_RGB_S3TC_DXT1_EXT;
                        size_t v14    = 4; // block w
                        size_t v16    = 4; // block h
                        size_t v15    = 8; // block size?
                        switch (txtr->texture_type) {
                        case 1: {
                            format   = GL_COMPRESSED_RGB_S3TC_DXT1_EXT;
                            auto rwb = (rpak_width + 3) / 4;
                            auto rhb = (rpak_height + 3) / 4;
                            rhsz     = (rwb * rhb) * 8ull;
                        } break;
                        case 13: {
                            format   = GL_COMPRESSED_RGBA_BPTC_UNORM;
                            auto rwb = (rpak_width + 3) / 4;
                            auto rhb = (rpak_height + 3) / 4;
                            rhsz     = (rwb * rhb) * 16ull;
                            v15      = 16ull;
                        } break;
                        default:
                            std::cout << " UNK" << +txtr->texture_type << ' ';
                            break;
                        }
                        const auto unk1e = txtr->layers_count ? txtr->layers_count : 1;
                        for (long long i = total_mipmaps; i > (total_mipmaps - txtr->rpak_mipmaps_num); i--) {
                            const auto v17 = ((txtr->width >> i) > 1) ? (txtr->width >> i) : 1;
                            const auto v22 = ((txtr->height >> i) > 1) ? (txtr->height >> i) : 1;

                            const auto v21 = (v14 + v17 - 1) / v14;
                            const auto v23 = v21 * ((v16 + v22 - 1) / v16);
                            const auto v25 = v15 * v23;

                            const auto sizee     = ((v25 + 15) & 0xFFFFFFF0);
                            const auto skip_size = unk1e * sizee;
                            data += skip_size;

                            rpak_width  = v17;
                            rpak_height = v22;
                            rhsz        = sizee;
                        }
                        std::cout << ' ' << rpak_width << 'x' << rpak_height << ' ';

                        GLuint gl_texture = 0;
                        glCreateTextures(GL_TEXTURE_2D, 1, &gl_texture);
                        glTextureStorage2D(gl_texture, 5, format, rpak_width, rpak_height);
                        glCompressedTextureSubImage2D(gl_texture, 0, 0, 0, rpak_width, rpak_height, format, rhsz, data);
                        glGenerateTextureMipmap(gl_texture);

                        texture_t texture;
                        texture.material_name          = surface_name;
                        texture.texture                = gl_texture;
                        stk_map.textures[surface_name] = std::move(texture);
                    } else {
                        std::cout << "FUCK_FILE " << std::hex << albedo << std::dec << ' ';
                    }
                }

                std::cout << std::endl; // it has flush but who cares about speed
            }

            texture_map_elem = stk_map.textures.find(surface_name);
            if (texture_map_elem != stk_map.textures.end()) {
                mp.texture  = texture_map_elem->second.texture;
                mp.textured = true;
            }

            constexpr auto unwanted_flags = uint32_t(VERTEX_FLAGS::SKY) | uint32_t(VERTEX_FLAGS::SKY_2D) | uint32_t(VERTEX_FLAGS::TRIGGER);
            if (mesh.flags & unwanted_flags) {
                memset(&mp.dec, 0, sizeof(mp.dec));
                std::cerr << "DISCARDING UNWANTED MESH WITH SIZE OF " << mesh.num_triangles << std::endl;
            }

            // TODO: subdata of a big buffer
            glCreateBuffers(1, &mp.dec_buf);
            glNamedBufferData(mp.dec_buf, static_cast<GLsizeiptr>(sizeof(mp.dec)), &mp.dec, GL_STATIC_DRAW);

            meshes_parsed[mesh_idx - model.first_mesh] = std::move(mp);
        }

        model_parsed_t model_parsed;
        model_parsed.meshes = std::move(meshes_parsed);
        models_parsed.push_back(model_parsed);
    }

    stk_map.models = std::move(models_parsed);

    return {true, stk_map}; // ???
}

void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    glViewport(0, 0, width, height);
}

bool shader_gen_glsl(GLuint program, uint32_t stage, const std::string& source) {
    auto data = source.c_str();

    auto shader = glCreateShader(stage);
    glShaderSource(shader, 1, &data, nullptr); // not SPIR-V shaders
    glCompileShader(shader);

    int32_t status;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (!status) {

        GLint maxLength = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &maxLength);
        std::vector<GLchar> errorLog(maxLength);
        glGetShaderInfoLog(shader, maxLength, &maxLength, &errorLog[0]);
        std::cerr << errorLog.data() << std::endl;

        glDeleteShader(shader);
        return false;
    }

    glAttachShader(program, shader);
    glLinkProgram(program);
    glDeleteShader(shader);

    glGetProgramiv(program, GL_LINK_STATUS, &status);
    return !!status;
}

auto program_gen(const std::string& v, const std::string& f) {
    auto program = glCreateProgram();
    glProgramParameteri(program, GL_PROGRAM_SEPARABLE, GL_TRUE);
    if (!shader_gen_glsl(program, GL_VERTEX_SHADER, v)) {
        std::cerr << "v err!" << std::endl;

        GLint maxLength = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &maxLength);
        std::vector<GLchar> errorLog(maxLength);
        glGetProgramInfoLog(program, maxLength, &maxLength, &errorLog[0]);
        std::cerr << errorLog.data() << std::endl;
    }
    if (!shader_gen_glsl(program, GL_FRAGMENT_SHADER, f)) {
        std::cerr << "f err!" << std::endl;

        GLint maxLength = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &maxLength);
        std::vector<GLchar> errorLog(maxLength);
        glGetProgramInfoLog(program, maxLength, &maxLength, &errorLog[0]);
        std::cerr << errorLog.data() << std::endl;
    }

    return program;
}

auto pipeline_gen(const std::string& v, const std::string& f) {
    GLuint pipeline;
    glCreateProgramPipelines(1, &pipeline);

    auto program = program_gen(v, f);
    glUseProgramStages(pipeline, GL_VERTEX_SHADER_BIT | GL_FRAGMENT_SHADER_BIT, program);

    return pipeline_t{
        program  = program,
        pipeline = pipeline,
    };
}

bool load_rpak(const char* name, RPak** res, std::vector<uint8_t>* res_data) {
    std::ifstream f(name, std::ifstream::binary);
    if (!f.fail()) {
        rpak_header_t header;
        f.read((char*)&header, sizeof(header));
        std::vector<uint8_t> fd(header.size_disk);
        f.read((char*)fd.data() + RPAK_HEADER_SIZE, fd.size() - RPAK_HEADER_SIZE);
        memcpy(fd.data(), &header, sizeof(header));
        uint64_t parameters[18];

        auto dsize = get_decompressed_size(uint64_t(parameters), fd.data(), -1i64, fd.size(), 0, RPAK_HEADER_SIZE);
        if (dsize != header.size_decompressed) {
            std::cerr << "Failed to load: " << name << ", DSIZE MISSMATCH!" << std::endl;
            *res = nullptr;
            return false;
        }

        std::vector<uint8_t> decompress_buffer(header.size_decompressed);
        parameters[1] = uint64_t(decompress_buffer.data());
        parameters[3] = -1;
        auto dret     = decompress_rpak((long long*)parameters, fd.size(), decompress_buffer.size());
        if (dret != 1) {
            std::cerr << "Failed to load: " << name << ", DRet was " << (+dret) << "!" << std::endl;
            *res = nullptr;
            return false;
        }

        memcpy(decompress_buffer.data(), &header, sizeof(header));
        *res_data = std::move(decompress_buffer);
        *res      = new RPak(res_data->data());

        return true;
    } else {
        *res = nullptr;
        return false;
    }
};

int main(int argc, char* argv[]) {
    if (!glfwInit()) {
        std::cerr << "GLFW fail!" << std::endl;
        return -1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE); // Macs smh
    GLFWwindow* window = glfwCreateWindow(DEFAULT_W, DEFAULT_H, "r5bsp", NULL, NULL);
    if (!window) {
        std::cerr << "Window fail! Make sure your GPU supports OGL4.6!" << std::endl;
        return -2;
    }

    glfwMakeContextCurrent(window);
    if (!gladLoadGLLoader(GLADloadproc(glfwGetProcAddress))) {
        std::cerr << "GLAD fail!" << std::endl;
        return -3;
    }

    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);

    // we generate all the necessary shit here before rendering
    // first is of course us
    auto pipeline = pipeline_gen(VERTEX_SHADER, FRAGMENT_SHADER);

    // second - ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    (void)io;
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 460");

    bool show_demo_window = true;
    bool show_menu        = true;

    stk_map_t map;
    struct {
        bool cull = false;
        // TODO???
        bool ignore_sky     = true;
        bool ignore_sky2d   = true;
        bool ignore_trigger = true;

        bool flat     = false;
        bool flat_nrm = false;
    } settings;

    {
        union brih {
            struct {
                uint8_t r;
                uint8_t g;
                uint8_t b;
            };
            uint8_t c[3];
        };
        brih       t[256 * 256];
        brih       col1{255, 0, 0};
        brih       col2{0, 0, 255};
        const auto checker_sie = 4;
        for (int y = 0; y < 256; y++)
            for (int x = 0; x < 256; x++) {
                auto c = x ^ y;
                // t[x + y * 256].r = t[x + y * 256].g = t[x + y * 256].b = c;
                if (((x & checker_sie) ^ (y & checker_sie))) {
                    t[x + y * 256].r = col1.r;
                    t[x + y * 256].g = col1.g;
                    t[x + y * 256].b = col1.b;
                } else {
                    t[x + y * 256].r = col2.r;
                    t[x + y * 256].g = col2.g;
                    t[x + y * 256].b = col2.b;
                }
            }
        glCreateTextures(GL_TEXTURE_2D, 1, &rpaks.error_texture);
        glTextureStorage2D(rpaks.error_texture, 8, GL_RGB8, 256, 256);
        glTextureSubImage2D(rpaks.error_texture, 0, 0, 0, 256, 256, GL_RGB, GL_UNSIGNED_BYTE, t);
        glGenerateTextureMipmap(rpaks.error_texture);

        glCreateSamplers(1, &rpaks.sampler);
        glSamplerParameteri(rpaks.sampler, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glSamplerParameteri(rpaks.sampler, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glSamplerParameteri(rpaks.sampler, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glSamplerParameteri(rpaks.sampler, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    // try loading rpaks...
    load_rpak("common_early.rpak", &rpaks.common_early, &rpaks.common_early_data);
    load_rpak("common.rpak", &rpaks.common, &rpaks.common_data);
    load_rpak("common_mp.rpak", &rpaks.common_mp, &rpaks.common_mp_data);

    const auto vec_up    = glm::vec3(0.f, 0.f, 1.f);
    const auto view_base = glm::lookAt(glm::vec3(0.f, 0.f, 0.f), glm::vec3(1000.f, 0.f, 0.f), vec_up);

    // const auto look_dir       = glm::vec3(1.f, 0.f, 0.f);
    // const auto look_dir_right = glm::cross(look_dir, vec_up);
    // const auto look_dir_up    = glm::cross(look_dir_right, look_dir);

    struct {
        struct {
            glm::mat4 proj = glm::perspective(glm::radians(90.f), float(DEFAULT_W) / DEFAULT_H, 16.f, 102400.f);
            glm::mat4 view;
        } opaque;
        glm::mat4 model = glm::scale(glm::mat4(1.0f), glm::vec3(1.f)); // meh?
    } shader_shit;
    shader_shit.opaque.view = view_base;

    GLuint ubuffer;
    glCreateBuffers(1, &ubuffer);
    glNamedBufferStorage(ubuffer, sizeof(shader_shit.opaque), &shader_shit.opaque, GL_DYNAMIC_STORAGE_BIT);

    glm::vec3 pos_delta(0.f, 0.f, 0.f);
    glm::vec3 rot_delta(glm::radians(90.f), 0.f, 0.f);

    auto timestamp = glfwGetTime();

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        if (glfwGetKey(window, GLFW_KEY_INSERT) == GLFW_PRESS) {
            show_menu = !show_menu;
        }

        auto now   = glfwGetTime();
        auto delta = float(now - timestamp);
        timestamp  = now;

        float SPEED = 4000.f;

        auto ubo_dirty = false;

        if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS) {
            rot_delta.y += glm::radians(45.f) * delta;
            ubo_dirty = true;
        }
        if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS) {
            rot_delta.y -= glm::radians(45.f) * delta;
            ubo_dirty = true;
        }
        if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS) {
            rot_delta.x += glm::radians(45.f) * delta;
            ubo_dirty = true;
        }
        if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS) {
            rot_delta.x -= glm::radians(45.f) * delta;
            ubo_dirty = true;
        }

        const auto look_dir_movement       = glm::normalize(glm::vec3(sinf(rot_delta.x), cosf(rot_delta.x), 0.f));
        const auto look_dir_right_movement = glm::cross(look_dir_movement, vec_up);

        const auto look_dir       = glm::normalize(glm::vec3(sinf(rot_delta.x) * cosf(rot_delta.y), cosf(rot_delta.x) * cosf(rot_delta.y), sinf(rot_delta.y)));
        const auto look_dir_right = glm::cross(look_dir, vec_up);
        const auto look_dir_up    = glm::cross(look_dir_right, look_dir);

        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) {
            // pos_delta.x += SPEED * delta;
            pos_delta += look_dir_movement * (SPEED * delta);
            ubo_dirty = true;
        }
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) {
            // pos_delta.x -= SPEED * delta;
            pos_delta -= look_dir_movement * (SPEED * delta);
            ubo_dirty = true;
        }
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
            // pos_delta.y += SPEED * delta;
            pos_delta -= look_dir_right_movement * (SPEED * delta);
            ubo_dirty = true;
        }
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
            // pos_delta.y -= SPEED * delta;
            pos_delta += look_dir_right_movement * (SPEED * delta);
            ubo_dirty = true;
        }
        if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) {
            pos_delta.z += SPEED * delta;
            ubo_dirty = true;
        }
        if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) {
            pos_delta.z -= SPEED * delta;
            ubo_dirty = true;
        }

        if (ubo_dirty) {
            // rot_delta %= glm::pi<float>();
            // auto rotate_matrix = glm::eulerAngleXY(rot_delta.x, rot_delta.y);
            // auto translation   = glm::translate(glm::mat4(1.f), pos_delta);

            // shader_shit.opaque.view = glm::translate(view_base, pos_delta);
            // shader_shit.opaque.view = rotate_matrix * glm::translate(view_base, pos_delta);
            shader_shit.opaque.view = glm::lookAt(pos_delta, pos_delta + look_dir, vec_up);
            glNamedBufferSubData(ubuffer, 0, static_cast<GLsizeiptr>(sizeof(shader_shit.opaque)), &shader_shit.opaque);
        }

        glClearColor(0, 1, 1, 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST);

        glDisable(GL_CULL_FACE);
        // glEnable(GL_CULL_FACE);
        // glCullFace(GL_FRONT);
        // glFrontFace(GL_CCW);

        if (map.loaded) {
            glBindProgramPipeline(pipeline.pipeline);
            glBindVertexArray(map.gl_vertex_array);

            glBindBufferBase(GL_UNIFORM_BUFFER, 0, ubuffer);
            glProgramUniformMatrix4fv(pipeline.program, 1, 1, GL_FALSE, (const GLfloat*)&shader_shit.model);

            glBindTextureUnit(0, rpaks.error_texture);
            glBindSampler(0, rpaks.sampler);
            glProgramUniform1i(pipeline.program, 2, settings.flat ? (settings.flat_nrm ? 2 : 0) : 1);

            for (const auto& models : map.models) {
                for (const auto& mesh : models.meshes) {
                    if (mesh.draw) {
                        if (!settings.flat) {
                            if (mesh.textured) {
                                glBindTextureUnit(0, mesh.texture);
                                // glBindSampler(0, rpaks.sampler);
                            } else {
                                glBindTextureUnit(0, rpaks.error_texture);
                                // glBindSampler(0, rpaks.sampler);
                                // continue;
                            }
                        }
                        glBindBuffer(GL_DRAW_INDIRECT_BUFFER, mesh.dec_buf);
                        glDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, nullptr);
                    }
                }
            }
        }

        // --- Start of ImGui ---
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // ImGui rendering here
        if (show_menu) {
            ImGui::ShowDemoWindow(&show_demo_window);
            if (ImGui::Begin("Info")) {
                ImGui::Text("X: %f", pos_delta.x);
                ImGui::Text("Y: %f", pos_delta.y);
                ImGui::Text("Z: %f", pos_delta.z);

                ImGui::Text("P: %f", rot_delta.x);
                ImGui::Text("Y: %f", rot_delta.y);
                ImGui::Text("R: %f", rot_delta.z);

                ImGui::Separator();

                ImGui::Checkbox("Flat?", &settings.flat);
                ImGui::Checkbox("Flat Normal?", &settings.flat_nrm);
                ImGui::Checkbox("Cull? (WIP)", &settings.cull);
            }
            ImGui::End();

            if (ImGui::Begin("Models")) {
                char buf[128];
                for (size_t model_idx = 0; model_idx < map.models.size(); model_idx++) {
                    auto& model = map.models[model_idx];
                    if (!model.meshes.size())
                        continue;
                    sprintf(buf, "Model%03llu[%llu]", model_idx, model.meshes.size());
                    if (ImGui::TreeNode(buf)) {
                        ImGui::PushID(model_idx);
                        if (ImGui::Button("Disable all")) {
                            for (auto& mesh : model.meshes) {
                                mesh.draw = false;
                            }
                        }
                        if (ImGui::Button("Enable all")) {
                            for (auto& mesh : model.meshes) {
                                mesh.draw = true;
                            }
                        }
                        for (size_t mesh_idx = 0; mesh_idx < model.meshes.size(); mesh_idx++) {
                            sprintf(buf, "Mesh%03llu[%u]", mesh_idx, model.meshes[mesh_idx].dec.indices);
                            ImGui::Checkbox(buf, &model.meshes[mesh_idx].draw);
                        }
                        ImGui::PopID();
                        ImGui::TreePop();
                        ImGui::Separator();
                    }
                }
            }
            ImGui::End();

            if (ImGui::BeginMainMenuBar()) {
                if (ImGui::BeginMenu("File")) {
                    if (ImGui::MenuItem("Open", "Ctrl+O")) {
                        auto selection = pfd::open_file::open_file("Open BSP", ".", {"BSP (*.bsp)", "*.bsp"}).result();
                        if (!selection.empty()) {
                            auto& selected = selection[0];
                            std::cout << "User selected file " << selected << std::endl;

                            auto dot_pos = selected.rfind('.');
                            // TODO: platform...
                            auto slash_pos = selected.rfind('\\');
                            if (slash_pos == std::string::npos)
                                slash_pos = selected.rfind('/');

                            const auto stem = selected.substr(slash_pos + 1, dot_pos - slash_pos - 1);
                            //std::cout << "Stem: " << stem << std::endl;
                            const auto rpak_map_name = stem + ".rpak";
                            std::cout << "RPak map: " << rpak_map_name << std::endl;
                            load_rpak(rpak_map_name.c_str(), &rpaks.map, &rpaks.map_data);

                            std::ifstream file(selected, std::ifstream::binary);
                            auto [succ, map_idk] = load_map(file);
                            if (succ) {
                                if (map.loaded) {
                                    glDeleteBuffers(1, &map.index_buffer);
                                    glDeleteBuffers(1, &map.vertex_buffer);
                                    glDeleteVertexArrays(1, &map.gl_vertex_array);

                                    for (const auto& model : map.models) {
                                        for (const auto& mesh : model.meshes) {
                                            glDeleteBuffers(1, &mesh.dec_buf);
                                        }
                                    }

                                    // I guess keeping them around is nice cuz they are just 2-4KiB at max?
                                    // for (const auto& texture : map.textures) {
                                    //     glDeleteTextures(1, &texture.second.texture);
                                    // }
                                }

                                map = std::move(map_idk);
                                GLuint buf[2];
                                glCreateBuffers(2, buf);
                                glNamedBufferData(buf[0], static_cast<GLsizeiptr>(map.index_vec.size() * sizeof(uint32_t)), map.index_vec.data(), GL_STATIC_DRAW);
                                glNamedBufferData(buf[1], static_cast<GLsizeiptr>(map.vertex_vec.size() * sizeof(stk_vertex_t)), map.vertex_vec.data(), GL_STATIC_DRAW);
                                map.index_buffer  = buf[0];
                                map.vertex_buffer = buf[1];

                                glCreateVertexArrays(1, &map.gl_vertex_array);
                                glVertexArrayElementBuffer(map.gl_vertex_array, map.index_buffer);
                                glVertexArrayVertexBuffer(map.gl_vertex_array, 0, map.vertex_buffer, 0, sizeof(stk_vertex_t));

                                // layout(location = 0) in vec3 vertPos;
                                glEnableVertexArrayAttrib(map.gl_vertex_array, 0);
                                glVertexArrayAttribFormat(map.gl_vertex_array, 0, 3, GL_FLOAT, GL_FALSE, offsetof(stk_vertex_t, pos));
                                glVertexArrayAttribBinding(map.gl_vertex_array, 0, 0);
                                // layout(location = 1) in vec3 vertNorm;
                                glEnableVertexArrayAttrib(map.gl_vertex_array, 1);
                                glVertexArrayAttribFormat(map.gl_vertex_array, 1, 3, GL_FLOAT, GL_FALSE, offsetof(stk_vertex_t, normal));
                                glVertexArrayAttribBinding(map.gl_vertex_array, 1, 0);

                                // layout(location = 3) in vec2 vertUV;
                                glEnableVertexArrayAttrib(map.gl_vertex_array, 3);
                                glVertexArrayAttribFormat(map.gl_vertex_array, 3, 2, GL_FLOAT, GL_TRUE, offsetof(stk_vertex_t, uv));
                                glVertexArrayAttribBinding(map.gl_vertex_array, 3, 0);

                                map.loaded = true;
                            }
                        }
                    }
                    ImGui::EndMenu();
                }
                ImGui::EndMainMenuBar();
            }
        }

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    // Delete our shit
    glDeleteProgramPipelines(1, &pipeline.pipeline);
    glDeleteProgram(pipeline.program);

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
#include "const.h"
#include "rashader.h"
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include <fstream>

#include "rapidjson/document.h"
#define RASHADER_SHADER_DIR "slang-shaders"

struct rashader_preset_t {
    std::string filename;
    std::unordered_map<std::string, float> params;
};

struct rashader_mode_t {
    std::string name;
    rashader_preset_t preset;
};

struct rashader_t {
    std::vector<rashader_mode_t> modes;
    std::vector<std::string> display_names;
};

static std::string read_file(const char *filename) {
    std::ifstream infile{filename};
    if (!infile)
        return {};
    std::istreambuf_iterator<char> itfilebegin{infile}, itfileend;
    return {itfilebegin, itfileend};
}

static bool load_mode(
    const rapidjson::GenericObject<true, rapidjson::Value>& mode_obj,
    rashader_mode_t& mode
) {
    auto name_node = mode_obj.FindMember("name");
    if (name_node == mode_obj.MemberEnd() || !name_node->value.IsString()) {
        return false;
    }
    mode.name = name_node->value.GetString();

    auto preset_node = mode_obj.FindMember("preset");
    if (preset_node == mode_obj.MemberEnd() || !preset_node->value.IsString()) {
        return false;
    }

    mode.preset.filename = RASHADER_SHADER_DIR "/";
    mode.preset.filename += preset_node->value.GetString();
    mode.preset.filename += ".slangp";

    auto params_node = mode_obj.FindMember("params");
    if (params_node != mode_obj.MemberEnd() && params_node->value.IsObject()) {
        auto params_obj = params_node->value.GetObj();

        mode.preset.params.reserve(params_obj.MemberCount());
        for (const auto &param : params_obj) {
            std::string name = param.name.GetString();
            float &value = mode.preset.params[name];
            if (param.value.IsNumber()) {
                value = param.value.GetFloat();
            }
        }
    }

    return true;
}

static std::vector<rashader_mode_t> parse_modes(const rapidjson::GenericObject<true, rapidjson::Value>& root) {
    std::vector<rashader_mode_t> modes;

    auto modes_node = root.FindMember("modes");
    if (modes_node == root.MemberEnd() || !modes_node->value.IsArray()) {
        return modes;
    }

    const auto& modes_array = modes_node->value.GetArray();
    const rapidjson::SizeType size = modes_array.Size();
    if (size == 0) {
        return modes;
    }

    modes.reserve(size);

    for (const auto& elem : modes_array) {
        if (!elem.IsObject()) {
            continue;
        }

        if (!load_mode(elem.GetObj(), modes.emplace_back())) {
            modes.pop_back();
        }
    }


    return modes;
}

struct rashader_t *rashader_load(const char *filename) {
    std::string json = read_file(filename);
    if (!json.size())
        return 0;

    rapidjson::Document doc;
    doc.ParseInsitu<rapidjson::kParseCommentsFlag | rapidjson::kParseTrailingCommasFlag>(json.data());
    if (doc.HasParseError()) {
        err_log("rashader config file \"%s\" parse error\n", filename);
        return 0;
    }

    if (!doc.IsObject()) {
        return 0;
    }

    auto rashader = std::make_unique<rashader_t>();
    rashader->modes = parse_modes(((const rapidjson::Document&)doc).GetObj());
    rashader->display_names.resize(rashader_mode_count(rashader.get()));

    return rashader.release();
}

void rashader_unload(struct rashader_t *rashader) {
    if (rashader)
        delete rashader;
}

size_t rashader_mode_count(struct rashader_t *rashader) {
    return rashader->modes.size();
}

const char *rashader_mode_name(struct rashader_t *rashader, size_t index, const char *prefix) {
    if (index >= rashader_mode_count(rashader)) {
        return 0;
    }

    if (rashader->display_names[index].empty()) {
        rashader->display_names[index] = prefix + rashader->modes[index].name;
    }

    return rashader->display_names[index].c_str();
}

struct rashader_render_t {
    libra_shader_preset_t preset;
    void *filter_chain;
};

struct rashader_render_t *rashader_render_init(struct rashader_t *rashader, size_t index, libra_preset_ctx_t *ctx, PFN_filter_chain_create fcc_fn, void *user) {
    auto render = std::make_unique<rashader_render_t>();

    struct libra_preset_opt_t opt = {
        .version = libra_instance_api_version(),
        // .frametime_uniforms = true,
    };
    auto &info = rashader->modes[index].preset;
    libra_shader_preset_t preset;
    libra_error_t err = libra_preset_create_with_options(info.filename.c_str(), ctx, &opt, &preset);
    if (err) {
        libra_error_print(err);
        libra_error_free(&err);
        goto fail;
    }

    for (auto &param : info.params) {
        err = libra_preset_set_param(&preset, param.first.c_str(), param.second);
        if (err) {
            libra_error_print(err);
            libra_error_free(&err);
        }
    }

    render->filter_chain = fcc_fn(&preset, user);
    if (!render->filter_chain) {
        goto fc_fail;
    }

    render->preset = preset;
    return render.release();

fc_fail:
    if (preset)
        libra_preset_free(&preset);
fail:
    return NULL;
}

void rashader_render_close(struct rashader_render_t *render, PFN_filter_chain_free fcf_fn) {
    if (render) {
        if (render->filter_chain)
            fcf_fn(&render->filter_chain);
        if (render->preset)
            libra_preset_free(&render->preset);
        delete render;
    }
}

void *rashader_render_chain(struct rashader_render_t *render) {
    return &render->filter_chain;
}

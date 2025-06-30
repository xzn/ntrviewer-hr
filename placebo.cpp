#include "const.h"
#include "placebo.h"
#include <string>
#include <optional>
#include <unordered_map>
#include <vector>
#include <memory>
#include <fstream>
#include <algorithm>

#include "rapidjson/document.h"
#define PLACEBO_SHADER_DIR "placebo-shaders"

pl_log placebo_log_create(void) {
    pl_log_params params = {
        .log_cb = pl_log_simple,
        .log_level = PL_LOG_ERR,
    };
    return pl_log_create(PL_API_VER, &params);
}

void placebo_log_destroy(pl_log *log) {
    if (*log) {
        if ((*log)->params.log_priv) {
            fclose((FILE *)(*log)->params.log_priv);
        }
        pl_log_destroy(log);
    }
}

struct placebo_param_value_t {
    std::optional<int> i;
    std::optional<unsigned> u;
    std::optional<float> f;
};

struct placebo_effect_t {
    std::string filename;
    std::unordered_map<std::string, placebo_param_value_t> params;
};

struct placebo_mode_t {
    std::string name;
    std::vector<placebo_effect_t> effects;
};

struct placebo_t {
    std::vector<placebo_mode_t> modes;
    std::vector<std::string> display_names;
};

static bool load_mode(
    const rapidjson::GenericObject<true, rapidjson::Value>& mode_obj,
    placebo_mode_t& mode
) {
    auto name_node = mode_obj.FindMember("name");
    if (name_node == mode_obj.MemberEnd() || !name_node->value.IsString()) {
        return false;
    }
    mode.name = name_node->value.GetString();

    auto effects_node = mode_obj.FindMember("effects");
    if (effects_node == mode_obj.MemberEnd() || !effects_node->value.IsArray()) {
        return true;
    }

    auto effects_array = effects_node->value.GetArray();
    mode.effects.reserve(effects_array.Size());

    for (const auto& elem : effects_array) {
        if (!elem.IsObject()) {
            continue;
        }

        auto elem_obj = elem.GetObj();
        placebo_effect_t& effect = mode.effects.emplace_back();

        auto name_node = elem_obj.FindMember("name");
        if (name_node == elem_obj.MemberEnd() || !name_node->value.IsString()) {
            mode.effects.pop_back();
            continue;
        }
        effect.filename = name_node->value.GetString();

        auto params_node = elem_obj.FindMember("params");
        if (params_node != elem_obj.MemberEnd() && params_node->value.IsObject()) {
            auto params_obj = params_node->value.GetObj();

            effect.params.reserve(params_obj.MemberCount());
            for (const auto& param : params_obj) {
                std::string name = param.name.GetString();
                placebo_param_value_t& value = effect.params[name];
                if (param.value.IsNumber()) {
                    value.f = param.value.GetFloat();
                }
                if (param.value.IsInt()) {
                    value.i = param.value.GetInt();
                }
                if (param.value.IsUint()) {
                    value.u = param.value.GetUint();
                }
            }
        }
    }

    return true;
}

static std::vector<placebo_mode_t> parse_modes(const rapidjson::GenericObject<true, rapidjson::Value>& root) {
    std::vector<placebo_mode_t> modes;

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

static std::string read_file(const char *filename) {
    std::ifstream infile{filename};
    if (!infile)
        return {};
    std::istreambuf_iterator<char> itfilebegin{infile}, itfileend;
    return {itfilebegin, itfileend};
}

struct placebo_t *placebo_load(const char *filename) {
    std::string json = read_file(filename);
    if (!json.size())
        return 0;

    rapidjson::Document doc;
    doc.ParseInsitu<rapidjson::kParseCommentsFlag | rapidjson::kParseTrailingCommasFlag>(json.data());
    if (doc.HasParseError()) {
        err_log("placebo config file \"%s\" parse error\n", filename);
        return 0;
    }

    if (!doc.IsObject()) {
        return 0;
    }

    auto placebo = std::make_unique<placebo_t>();
    placebo->modes = parse_modes(((const rapidjson::Document&)doc).GetObj());
    placebo->display_names.resize(placebo_mode_count(placebo.get()));

    return placebo.release();
}

void placebo_unload(struct placebo_t *placebo) {
    if (placebo)
        delete placebo;
}

size_t placebo_mode_count(struct placebo_t *placebo) {
    return placebo->modes.size();
}

const char *placebo_mode_name(struct placebo_t *placebo, size_t index, const char *prefix) {
    if (index >= placebo_mode_count(placebo)) {
        return 0;
    }

    if (placebo->display_names[index].empty()) {
        placebo->display_names[index] = prefix + placebo->modes[index].name;
    }

    return placebo->display_names[index].c_str();
}

extern "C" {

#include <libplacebo/renderer.h>
#include <libplacebo/utils/upload.h>

}

struct placebo_render_t {
    pl_renderer render;
    pl_render_params render_params;
    std::vector<const pl_hook *> hooks;
    pl_tex out_tex;
    pl_gpu gpu; // non-owning
    int out_width, out_height;
};

struct placebo_render_t *placebo_render_init(struct placebo_t *placebo, size_t index, pl_gpu gpu, pl_log log) {
    if (index >= placebo_mode_count(placebo))
        return 0;

    auto render = std::make_unique<placebo_render_t>();

    memcpy(&render->render_params, &pl_render_fast_params, sizeof(pl_render_params));
    render->render_params.upscaler = &pl_filter_bilinear;
    render->render_params.downscaler = &pl_filter_bilinear;

    auto &mode = placebo->modes[index];

    render->hooks.resize(mode.effects.size());
    for (int i = 0; i < (int)render->hooks.size(); ++i) {
        auto &hook = render->hooks[i];
        auto &effect = mode.effects[i];

        std::string shader = read_file((PLACEBO_SHADER_DIR "/" + effect.filename + ".glsl").c_str());
        if (!shader.size()) {
            err_log("placebo read shader failed %s\n", effect.filename.c_str());
            goto fail;
        }

        hook = pl_mpv_user_shader_parse(gpu, shader.data(), shader.size());
        if (!hook) {
            err_log("placebo parse shader failed\n");
            goto fail;
        }

        for (int j = 0; j < hook->num_parameters; ++j) {
            const pl_hook_par &par = hook->parameters[j];
            auto it = effect.params.find(par.name);
            if (it != effect.params.end()) {
                auto &val = it->second;

                switch (par.type) {
                    case PL_VAR_SINT:
                        if (val.i)
                            par.data->i = std::clamp(val.i.value(), par.minimum.i, par.maximum.i);
                        break;

                    case PL_VAR_UINT:
                        if (val.u)
                            par.data->u = std::clamp(val.u.value(), par.minimum.u, par.maximum.u);
                        break;

                    case PL_VAR_FLOAT:
                        if (val.f)
                            par.data->f = std::clamp(val.f.value(), par.minimum.f, par.maximum.f);
                        break;

                    default:
                        break;
                }
            }
        }
    }

    render->render_params.num_hooks = render->hooks.size();
    render->render_params.hooks = render->hooks.data();

    render->render = pl_renderer_create(log, gpu);
    if (!render->render) {
        err_log("placebo create renderer failed\n");
        goto fail;
    }

    render->gpu = gpu;

    return render.release();

fail:
    placebo_render_close(render.release());
    return 0;
}

void placebo_render_close(struct placebo_render_t *render) {
    if (render) {
        for (auto &hook : render->hooks) {
            pl_mpv_user_shader_destroy(&hook);
        }
        pl_renderer_destroy(&render->render);
        delete render;
    }
}

pl_tex placebo_render_run(struct placebo_render_t *render, pl_tex in_tex, pl_tex out_tex, int out_width, int out_height) {
    if (!out_tex) {
        pl_fmt out_fmt = pl_find_fmt(render->gpu, PL_FMT_UNORM, 4, 0, 0, pl_fmt_caps(PL_FMT_CAP_SAMPLEABLE | PL_FMT_CAP_RENDERABLE));
        if (!out_fmt)
            return 0;

        if (!render->out_tex || render->out_width != out_width || render->out_height != out_height) {
            pl_tex_params out_tex_pars = {
                .w = out_width,
                .h = out_height,
                .d = 0,
                .format = out_fmt,
                .sampleable = 1,
                .renderable = 1,
            };

            if (!pl_tex_recreate(render->gpu, &render->out_tex, &out_tex_pars))
                return 0;
        }

        pl_frame image = {
            .num_planes = 1,
            .planes = {{
                .texture = in_tex,
                .components = 4,
                .component_mapping = {0, 1, 2, 3},
            }},
            .repr = pl_color_repr_rgb,
            .color = pl_color_space_monitor,
        };

        pl_frame target = image;
        target.planes[0].texture = render->out_tex;

        if (!pl_render_image(render->render, &image, &target, &render->render_params))
            return 0;

        pl_render_errors err = pl_renderer_get_errors(render->render);
        if (err.errors != PL_RENDER_ERR_NONE)
            return 0;

        return render->out_tex;
    } else {
        pl_frame image = {
            .num_planes = 1,
            .planes = {{
                .texture = in_tex,
                .components = 4,
                .component_mapping = {0, 1, 2, 3},
            }},
            .repr = pl_color_repr_rgb,
            .color = pl_color_space_monitor,
        };

        pl_frame target = image;
        target.planes[0].texture = out_tex;

        if (!pl_render_image(render->render, &image, &target, &render->render_params))
            return 0;

        pl_render_errors err = pl_renderer_get_errors(render->render);
        if (err.errors != PL_RENDER_ERR_NONE)
            return 0;

        return out_tex;
    }
}

#include "core/serialization/serialization_common.h"
#include "core/serialization/serialization_2.h"
#include "core/imgui/imgui_bindings_common.h"

#include "core/GraphicsHelpers.h"

#include "core/messages/messages_rendering.h"

REFL_ATTRIBUTES(REFL_NO_SKIP)
class crap
{
    HA_MESSAGES_IN_MIXIN(crap);
};

HA_MIXIN_DEFINE(crap, dynamix::none)

class mesh
{
    HA_MESSAGES_IN_MIXIN(mesh);

    static void mesh_changed_cb(mesh& in) { in._mesh = MeshMan::get().get(in._path); }

    FIELD MeshHandle _mesh;
    FIELD ShaderHandle _shader;
    REFL_ATTRIBUTES(tag::mesh, REFL_CALLBACK(mesh::mesh_changed_cb))
    FIELD std::string _path;
    REFL_ATTRIBUTES(tag::image)
    FIELD std::string _image_path;

    FIELD bool   clicky      = false;
    FIELD float  dragy       = 42;
    FIELD double dragy2      = 42;
    FIELD int    dragy3      = 42;
    FIELD std::string texty  = "happy!!";
    FIELD std::string texty2 = ":(";
    FIELD oid some_obj_id;
    FIELD std::vector<oid> omg;
    FIELD std::map<int, oid> omg2;

public:
    mesh() {
        _path = "meshes/bunny.bin";

        _mesh   = MeshMan::get().get(_path);
        _shader = ShaderMan::get().get("mesh");

        //omg.resize(5);
        //omg2.insert({5, oid()});
    }

    void get_rendering_parts(std::vector<renderPart>& out) const {
        out.push_back({_mesh, {}, _shader, ha_this.get_transform().as_mat()});
    }

    AABB get_aabb() const { return getMeshBBox(_mesh.get()); }
};

HA_MIXIN_DEFINE(mesh, rend::get_rendering_parts_msg& rend::get_aabb_msg)

#include <gen/common.cpp.inl>

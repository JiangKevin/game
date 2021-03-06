#pragma once

#include "core/PagedMixinAllocator.h"

// =================================================================================================
// ==  MIXINS ======================================================================================
// =================================================================================================

typedef std::map<Object*, JsonData> ObjectJsonMap;
typedef void (*load_unload_proc)(ObjectJsonMap&);
typedef void (*mutate_proc)(Object*);
typedef void (*update_proc)(float dt);
typedef const dynamix::internal::mixin_type_info* (*get_mixin_type_info_proc)();
typedef const std::vector<oid>& (*get_objects_proc)();

struct MixinInfo
{
    mutate_proc              add;
    mutate_proc              remove;
    load_unload_proc         load;
    load_unload_proc         unload;
    update_proc              update;
    get_mixin_type_info_proc get_mixin_type_info;
    get_objects_proc         get_objects;
};

typedef std::map<std::string, MixinInfo> MixinInfoMap;
typedef MixinInfoMap& (*get_mixins_proc)();

// defined in the executable - use this from plugins to get to the list of all registered mixins and not "getMixins()"
// because that would lead to linker errors since both the plugin and the executable export that symbol - and even if
// there was no linker error - calling it from a plugin would still result in getting only the list of mixins in that plugin
#ifdef HA_PLUGIN
HA_SYMBOL_IMPORT MixinInfoMap& getAllMixins();
#else  // HA_PLUGIN
HA_SYMBOL_EXPORT MixinInfoMap& getAllMixins();
#endif // HA_PLUGIN

HA_SUPPRESS_WARNINGS
extern "C" HA_SYMBOL_EXPORT MixinInfoMap& getMixins();
HA_SUPPRESS_WARNINGS_END

// use this in plugins to not get linker errors but still be able to get your own mixins
MixinInfoMap& getMixins_Local();

int registerMixin(cstr name, MixinInfo info);

template <typename T>
struct UpdatableMixin
{
    static void call_update(float dt) {
        const auto& allocator  = PagedMixinAllocator<T>::get();
        const auto& flags      = allocator.getAllocatedFlags();
        const auto  flags_size = flags.size();

        for(size_t i = 0; i < flags_size; ++i)
            if(flags[i])
                allocator[i].update(dt);
    }
};

template <typename T>
typename std::enable_if<std::is_base_of<UpdatableMixin<T>, T>::value, update_proc>::type
getUpdateProc() {
    return UpdatableMixin<T>::call_update;
}

template <typename T>
typename std::enable_if<!std::is_base_of<UpdatableMixin<T>, T>::value, update_proc>::type
getUpdateProc() {
    return nullptr;
}

template <typename T>
load_unload_proc getLoadProc() {
    return [](ObjectJsonMap& in) {
        for(auto& curr : in) {
            dynamix::mutate(curr.first).add<T>();
            const sajson::document& doc = curr.second.parse();
            hassert(doc.is_valid());
            deserialize(*curr.first->get<T>(), doc.get_root());
        }
    };
}

template <typename T>
load_unload_proc getUnloadProc() {
    return [](ObjectJsonMap& out) {
        const auto& allocator  = PagedMixinAllocator<T>::get();
        const auto& flags      = allocator.getAllocatedFlags();
        const auto  flags_size = flags.size();

        for(size_t i = 0; i < flags_size; ++i) {
            if(flags[i]) {
                auto& mixin  = allocator[i];
                auto& object = Object::cast_to_object(&mixin);
                out[&object].reserve(200); // for small mixins - just 1 allocation
                serialize(mixin, out[&object]);
            }
        }
        for(auto& curr : out)
            dynamix::mutate(curr.first).remove<T>();
    };
}

#ifdef HA_PLUGIN
#define HA_MIXIN_DEFINE_IN_PLUGIN_LOAD(n) getLoadProc<n>()
#define HA_MIXIN_DEFINE_IN_PLUGIN_UNLOAD(n) getUnloadProc<n>()
#else // HA_PLUGIN
#define HA_MIXIN_DEFINE_IN_PLUGIN_LOAD(n) nullptr
#define HA_MIXIN_DEFINE_IN_PLUGIN_UNLOAD(n) nullptr
#endif // HA_PLUGIN

// this differs from DYNAMIX_DEFINE_MIXIN in that it explicitly registers the mixin while all globals in a
// translation unit are initialized from top to bottom - and not at the end because of some template instantiation
#define DYNAMIX_DEFINE_MIXIN_2(mixin_type, mixin_features)                                         \
    /* create a mixin_type_info getter for this type */                                            \
    ::dynamix::internal::mixin_type_info& _dynamix_get_mixin_type_info(const mixin_type*) {        \
        return ::dynamix::internal::mixin_type_info_instance<mixin_type>::info();                  \
    }                                                                                              \
    HA_UNUSED_GLOBAL_NO_WARNINGS(                                                                  \
            HA_CAT_1(_mixin_register_internal_omgomgomg_, mixin_type)) = []() {                    \
        if(_dynamix_get_mixin_type_info((mixin_type*)nullptr).id == dynamix::INVALID_MIXIN_ID)     \
            dynamix::internal::domain::safe_instance().register_mixin_type<mixin_type>(            \
                    _dynamix_get_mixin_type_info((mixin_type*)nullptr));                           \
        return 0;                                                                                  \
    }();                                                                                           \
    HA_UNUSED_GLOBAL_NO_WARNINGS_END                                                               \
    /* create a features parsing function */                                                       \
    /* features can be parsed multiple times by different parsers */                               \
    template <typename FeaturesParser>                                                             \
    void _dynamix_parse_mixin_features(const mixin_type*, FeaturesParser& parser) {                \
        parser& I_DYNAMIX_MIXIN_NAME_FEATURE(#mixin_type) & mixin_features;                        \
    }

#define HA_MIXIN_DEFINE_COMMON(n, features)                                                        \
    template <>                                                                                    \
    PagedMixinAllocator<n>* PagedMixinAllocator<n>::instance = nullptr;                            \
    DYNAMIX_DEFINE_MIXIN_2(n, (PagedMixinAllocator<n>::constructGlobalInstance()) & features)      \
    HA_UNUSED_GLOBAL_NO_WARNINGS(HA_CAT_1(_mixin_register_, n)) =                                  \
            registerMixin(#n, /* force new line for format */                                      \
                          {[](Object* o) { dynamix::mutate(o).add<n>(); },    /**/                 \
                           [](Object* o) { dynamix::mutate(o).remove<n>(); }, /**/                 \
                           HA_MIXIN_DEFINE_IN_PLUGIN_LOAD(n),                 /**/                 \
                           HA_MIXIN_DEFINE_IN_PLUGIN_UNLOAD(n),               /**/                 \
                           getUpdateProc<n>(),                                /**/                 \
                           []() {                                                                  \
                               return static_cast<const dynamix::internal::mixin_type_info*>(      \
                                       &_dynamix_get_mixin_type_info((n*)nullptr));                \
                           },                                                                      \
                           []() -> decltype(PagedMixinAllocator<n>::get().getObjects()) {          \
                               return PagedMixinAllocator<n>::get().getObjects();                  \
                           }});                                                                    \
    HA_UNUSED_GLOBAL_NO_WARNINGS_END

#define HA_MIXIN_DEFINE(n, f)                                                                      \
    void n::serialize_mixins(cstr concrete_mixin, JsonData& out) const {                           \
        if(concrete_mixin && strcmp(#n, concrete_mixin) != 0)                                      \
            return;                                                                                \
        out.addKey(#n);                                                                            \
        serialize(*this, out);                                                                     \
        out.addComma();                                                                            \
    }                                                                                              \
    void n::deserialize_mixins(const sajson::value& in) {                                          \
        auto str = sajson::string(#n, HA_COUNT_OF(#n) - 1);                                        \
        if(in.find_object_key(str) != in.get_length())                                             \
            deserialize(*this, in.get_value_of_key(str));                                          \
    }                                                                                              \
    HA_MIXIN_DEFINE_COMMON(n, common::serialize_mixins_msg& common::deserialize_mixins_msg&        \
                                      common::get_imgui_binding_callbacks_from_mixins_msg& f)      \
    void n::get_imgui_binding_callbacks_from_mixins(imgui_binding_callbacks& cbs) {                \
        cbs.push_back({&_dynamix_get_mixin_type_info((n*)nullptr),                                 \
                       [](Object& obj) { imgui_bind_attributes(obj, #n, *obj.get<n>()); }});       \
    }

//common::gather_oids_mixins_msg&

//void n::gather_oids_mixins(std::vector<const_oid*>& in) { gather_oids(*this, in); }

#define HA_MIXIN_DEFINE_WITHOUT_CODEGEN(n, f) HA_MIXIN_DEFINE_COMMON(n, f)

// =================================================================================================
// ==  GLOBALS =====================================================================================
// =================================================================================================

typedef void (*serialize_global_proc)(JsonData&);
typedef void (*deserialize_global_proc)(const sajson::value&);

struct GlobalInfo
{
    serialize_global_proc   serialize;
    deserialize_global_proc deserialize;
};

typedef std::map<std::string, GlobalInfo> GlobalInfoMap;
typedef GlobalInfoMap& (*get_globals_proc)();

HA_SUPPRESS_WARNINGS
extern "C" HA_SYMBOL_EXPORT GlobalInfoMap& getGlobals();
HA_SUPPRESS_WARNINGS_END

int registerGlobal(cstr name, GlobalInfo info);

#define HA_GLOBAL_GEN_NAME(type, name) #type "_" HA_TOSTR(name) // "_" __FILE__

#define HA_GLOBAL_COMMON(type, name)                                                               \
    HA_UNUSED_GLOBAL_NO_WARNINGS(HA_ANONYMOUS(_global_)) = registerGlobal(                         \
            HA_GLOBAL_GEN_NAME(type, name),                                                        \
            {[](JsonData& out) { HA_SERIALIZE_VARIABLE(HA_GLOBAL_GEN_NAME(type, name), name); },   \
             [](const sajson::value& val) {                                                        \
                 deserialize(name, val.get_value_of_key(sajson::string(                            \
                                           HA_GLOBAL_GEN_NAME(type, name),                         \
                                           HA_COUNT_OF(HA_GLOBAL_GEN_NAME(type, name)) - 1)));     \
             }}) HA_UNUSED_GLOBAL_NO_WARNINGS_END

#define HA_GLOBAL(type, name)                                                                      \
    extern type name;                                                                              \
    HA_GLOBAL_COMMON(type, name);                                                                  \
    type name

#define HA_GLOBAL_STATIC(type, name)                                                               \
    static type name;                                                                              \
    HA_GLOBAL_COMMON(type, name)

#define HA_GLOBAL_MEMBER(type, class_type, name)                                                   \
    HA_GLOBAL_COMMON(type, HA_CAT_2(HA_CAT_2(class_type, ::), name));                              \
    type class_type::name

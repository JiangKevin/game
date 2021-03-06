#include "editor.h"

#include "core/serialization/serialization_common.h"

#include "core/messages/messages_editor.h"

HA_SUPPRESS_WARNINGS
#include <boost/range/adaptor/reversed.hpp>
HA_SUPPRESS_WARNINGS_END

static JsonData mixin_state(const Object& obj, cstr mixin) {
    JsonData out(1000);
    out.startObject();
    if(mixin && mixin[0] == '\0') {
        out.addKey("");
        serialize(obj, out);
    } else {
        if(obj.implements(common::serialize_mixins_msg))
            common::serialize_mixins(obj, mixin, out);
    }
    out.endObject();
    return out;
}

static std::vector<std::string> mixin_names(const Object& obj) {
    std::vector<cstr> mixins;
    obj.get_mixin_names(mixins);
    std::vector<std::string> out(mixins.size());
    std::transform(mixins.begin(), mixins.end(), out.begin(), [](auto in) { return in; });
    return out;
}

static compound_cmd objects_set_parent(const std::vector<oid>& objects, oid new_parent) {
    compound_cmd comp_cmd;
    comp_cmd.description = "setting a parent";

    // save the transforms (+parental) of the selected objects before changing parental information
    std::vector<std::pair<oid, std::pair<transform, JsonData>>> old_state;

    for(auto& curr : objects) {
        // record the old transform
        old_state.push_back({curr, {curr.obj().get_transform(), mixin_state(curr.obj(), "")}});

        // old parent old state
        auto     parent = curr.obj().get_parent();
        JsonData parent_old;
        if(parent)
            parent_old = mixin_state(parent.obj(), "");

        // set new parental relationship
        curr.obj().set_parent(new_parent);

        // old parent new state & command submit
        if(parent)
            comp_cmd.commands.push_back(
                    attributes_changed_cmd({parent, parent.obj().name(), parent_old,
                                            mixin_state(parent.obj(), ""), "parental"}));
    }

    for(auto& curr : old_state) {
        // set the old world transform (will recalculate the local transform of the object)
        curr.first.obj().set_transform(curr.second.first);
        // add the changed transform/parental information to the undo/redo command list
        comp_cmd.commands.push_back(
                attributes_changed_cmd({curr.first, curr.first.obj().name(), curr.second.second,
                                        mixin_state(curr.first.obj(), ""), "transform+parental"}));
    }

    return comp_cmd;
}

// filters out objects which are children (immediate or not) of the passed list of objects
std::vector<oid> get_top_most(const std::vector<oid>& from) {
    const std::set<oid> from_set{from.begin(), from.end()};
    std::vector<oid>    top_most;
    for(auto& curr : from) {
        // search for the current object (and its parents) in the full set of objects
        oid  parent = curr;
        bool found  = false;
        do {
            parent = parent.obj().get_parent();
            if(from_set.count(parent))
                found = true;
        } while(parent && !found);
        // if no parent of the current object is also in the set of objects
        if(!found)
            top_most.push_back(curr);
    }
    return top_most;
}

// selected is problematic because we are looping over a container in it...
// also camera is problematic because it is registered as an input event listener and I got a crash when I undid it's removal...
static bool cant_remove_mixin(cstr in) {
    return strcmp(in, "selected") == 0 || strcmp(in, "maya_camera") == 0 ||
           strcmp(in, "gameplay_camera") == 0;
}

// =================================================================================================
// == EDITOR IMPLEMENTATION ========================================================================
// =================================================================================================

void editor::create_object() {
    compound_cmd comp_cmd;
    comp_cmd.description = "creating an object";

    auto& obj = ObjectManager::get().create();
    comp_cmd.commands.push_back(
            object_creation_cmd({obj.id(), obj.name(), mixin_state(obj, ""), true}));
    comp_cmd.commands.push_back(object_mutation_cmd(
            {obj.id(), obj.name(), mixin_names(obj), mixin_state(obj, nullptr), true}));

    auto selected_objects_copy = getAllMixins()["selected"].get_objects();
    comp_cmd.commands.push_back(update_selection_cmd({obj.id()}, selected_objects_copy));

    add_command(comp_cmd);
}

void editor::add_mixins_to_selected(std::vector<const mixin_type_info*> mixins) {
    compound_cmd comp_cmd;
    comp_cmd.description = "selecting mixins";

    for(auto& id : getAllMixins()["selected"].get_objects()) {
        auto& obj = id.obj();

        for(auto& mixin : mixins) {
            if(obj.has(mixin->id))
                continue;

            obj.addMixin(mixin->name);
            comp_cmd.commands.push_back(object_mutation_cmd(
                    {id, id.obj().name(), {mixin->name}, mixin_state(obj, mixin->name), true}));
        }
    }

    if(!comp_cmd.commands.empty())
        add_command(comp_cmd);
}

void editor::remove_mixins_by_name_from_selected(std::vector<const mixin_type_info*> mixins) {
    compound_cmd comp_cmd;
    comp_cmd.description = "by name from selected";

    for(auto& id : getAllMixins()["selected"].get_objects()) {
        auto& obj = id.obj();

        for(auto& mixin : mixins) {
            if(!obj.has(mixin->id) || cant_remove_mixin(mixin->name))
                continue;

            obj.remMixin(mixin->name);
            comp_cmd.commands.push_back(object_mutation_cmd(
                    {id, id.obj().name(), {mixin->name}, mixin_state(obj, mixin->name), false}));
        }
    }

    if(!comp_cmd.commands.empty())
        add_command(comp_cmd);
}

void editor::remove_selected_mixins() {
    compound_cmd comp_cmd;
    comp_cmd.description = "selected mixins from selected objects";

    for(auto& id : getAllMixins()["selected"].get_objects()) {
        auto&                       selected_mixins = (*id.obj().get<selected>()).selected_mixins;
        std::set<dynamix::mixin_id> mixins_to_delete;

        for(auto& mixin : selected_mixins) {
            if(cant_remove_mixin(mixin.second.c_str()))
                continue;

            // remove the mixin
            comp_cmd.commands.push_back(
                    object_mutation_cmd({id,
                                         id.obj().name(),
                                         {mixin.second},
                                         mixin_state(id.obj(), mixin.second.c_str()),
                                         false}));
            id.obj().remMixin(mixin.second.c_str());

            mixins_to_delete.insert(mixin.first);
        }

        // update the selected_mixins list in "selected"
        if(!mixins_to_delete.empty()) {
            JsonData ov = mixin_attr_state("selected", "selected_mixins", selected_mixins);
            for(auto& curr : mixins_to_delete)
                selected_mixins.erase(curr);
            JsonData nv = mixin_attr_state("selected", "selected_mixins", selected_mixins);
            comp_cmd.commands.push_back(
                    attributes_changed_cmd({id, id.obj().name(), ov, nv, "selected"}));
        }
    }

    if(!comp_cmd.commands.empty())
        add_command(comp_cmd);
}

compound_cmd editor::update_selection_cmd(const std::vector<oid>& to_select,
                                          const std::vector<oid>& to_deselect) {
    compound_cmd comp_cmd;
    comp_cmd.description = "selection";

    if(to_select.size() + to_deselect.size() > 0) {
        comp_cmd.commands.reserve(to_select.size() + to_deselect.size());

        auto add_mutate_command = [&](oid id, bool select) {
            comp_cmd.commands.push_back(object_mutation_cmd({id,
                                                             id.obj().name(),
                                                             {"selected"},
                                                             mixin_state(id.obj(), "selected"),
                                                             select}));
        };

        for(auto curr : to_deselect) {
            add_mutate_command(curr, false);
            curr.obj().remMixin("selected");
        }
        for(auto curr : to_select) {
            curr.obj().addMixin("selected");
            add_mutate_command(curr, true);
        }
    }

    return comp_cmd;
}

void editor::update_selection(const std::vector<oid>& to_select,
                              const std::vector<oid>& to_deselect) {
    auto command = update_selection_cmd(to_select, to_deselect);
    if(!command.commands.empty())
        add_command(command, true);
}

void editor::handle_gizmo_changes() {
    // do not continue if nothing has changed
    auto pos   = yama::vector3::from_ptr(&gizmo_transform.position[0]);
    auto pos_l = yama::vector3::from_ptr(&gizmo_transform_last.position[0]);
    auto scl   = yama::vector3::from_ptr(&gizmo_transform.scale[0]);
    auto scl_l = yama::vector3::from_ptr(&gizmo_transform_last.scale[0]);
    auto rot   = yama::quaternion::from_ptr(&gizmo_transform.orientation[0]);
    auto rot_l = yama::quaternion::from_ptr(&gizmo_transform_last.orientation[0]);
    if(yama::close(pos, pos_l) && yama::close(scl, scl_l) && yama::close(rot, rot_l))
        return;

    compound_cmd comp_cmd;
    comp_cmd.description = "gizmo transform";

    for(auto& id : getAllMixins()["selected"].get_objects()) {
        auto old_t = id.obj().get<selected>()->old_local_t;
        auto new_t = id.obj().get_transform_local();
        if(!yama::close(old_t.pos, new_t.pos)) {
            JsonData ov = mixin_attr_state("", "pos", old_t.pos);
            JsonData nv = mixin_attr_state("", "pos", new_t.pos);
            comp_cmd.commands.push_back(
                    attributes_changed_cmd({id, id.obj().name(), ov, nv, "obj.pos"}));
        }
        if(!yama::close(old_t.scl, new_t.scl)) {
            JsonData ov = mixin_attr_state("", "scl", old_t.scl);
            JsonData nv = mixin_attr_state("", "scl", new_t.scl);
            comp_cmd.commands.push_back(
                    attributes_changed_cmd({id, id.obj().name(), ov, nv, "obj.scl"}));
        }
        if(!yama::close(old_t.rot, new_t.rot)) {
            JsonData ov = mixin_attr_state("", "rot", old_t.rot);
            JsonData nv = mixin_attr_state("", "rot", new_t.rot);
            comp_cmd.commands.push_back(
                    attributes_changed_cmd({id, id.obj().name(), ov, nv, "obj.rot"}));
        }
        // update this - even though we havent started using the gizmo - or else this might break when deleting the object
        id.obj().get<selected>()->old_t       = id.obj().get_transform();
        id.obj().get<selected>()->old_local_t = id.obj().get_transform_local();
    }
    if(!comp_cmd.commands.empty())
        add_command(comp_cmd);
}

void editor::reparent(oid new_parent_for_selected) {
    // detect cycles - the new parent shouldn't be a child (close or distant) of any of the selected objects
    auto& selected_objs = getAllMixins()["selected"].get_objects();
    if(new_parent_for_selected)
        for(auto curr = new_parent_for_selected; curr; curr = curr.obj().get_parent())
            if(std::find(selected_objs.begin(), selected_objs.end(), curr) != selected_objs.end())
                new_parent_for_selected = oid::invalid(); // set it to an invalid state

    // if selected objects have been dragged with the middle mouse button onto an unselected object - make them its children
    if(new_parent_for_selected) {
        compound_cmd comp_cmd;
        comp_cmd.description = "reparenting selected";

        auto new_parent_old = mixin_state(new_parent_for_selected.obj(), "");

        comp_cmd.commands.push_back(objects_set_parent(selected_objs, new_parent_for_selected));

        // update the parental part of the new parent
        comp_cmd.commands.push_back(attributes_changed_cmd(
                {new_parent_for_selected, new_parent_for_selected.obj().name(), new_parent_old,
                 mixin_state(new_parent_for_selected.obj(), ""), "parental"}));

        // add the compound command
        add_command(comp_cmd);
    }
}

void editor::group_selected() {
    auto selected_objs_copy = getAllMixins()["selected"].get_objects();
    if(selected_objs_copy.empty())
        return;

    compound_cmd comp_cmd;
    comp_cmd.description = "grouping selected";

    auto find_lowest_common_ancestor = [&]() {
        // go upwards from each selected node and update the visited count for each node
        std::map<oid, int> visited_counts;
        for(auto curr : selected_objs_copy) {
            while(curr != oid::invalid()) {
                visited_counts[curr]++;
                curr = curr.obj().get_parent();
            }
        }

        // remove any node that has been visited less times than the number of selected objects
        Utils::erase_if(visited_counts,
                        [&](auto in) { return in.second < int(selected_objs_copy.size()); });

        // if there is a common ancestor - it will have the same visited count as the number of selected objects
        if(visited_counts.size() == 1 &&
           std::find(selected_objs_copy.begin(), selected_objs_copy.end(),
                     visited_counts.begin()->first) == selected_objs_copy.end()) {
            // if only one object is left after the filtering (common ancestor to all) and is not part of the selection
            return visited_counts.begin()->first;
        } else if(visited_counts.size() > 1) {
            // if atleast 2 nodes have the same visited count - means that one of the selected nodes
            // is also a common ancestor (also to itself) - we need to find it and get its parent
            for(auto& curr : visited_counts)
                if(curr.first.obj().has<selected>())
                    return curr.first.obj().get_parent();
        }
        // all other cases
        return oid::invalid();
    };

    // create new group object
    auto& group = ObjectManager::get().create("group");

    // if there is a common ancestor - add the new group object as its child
    auto common_ancestor = find_lowest_common_ancestor();
    if(common_ancestor) {
        JsonData ancestor_old = mixin_state(common_ancestor.obj(), "");
        group.set_parent(common_ancestor);
        comp_cmd.commands.push_back(
                attributes_changed_cmd({common_ancestor, common_ancestor.obj().name(), ancestor_old,
                                        mixin_state(common_ancestor.obj(), ""), "parental"}));
    }

    // average position for the new group object
    auto average_pos = yama::vector3::zero();
    for(auto& curr : selected_objs_copy)
        average_pos += curr.obj().get_pos();
    average_pos /= float(selected_objs_copy.size());
    // set position of newly created group to be the average position of all selected objects
    group.set_transform({average_pos, {1, 1, 1}, {0, 0, 0, 1}});

    comp_cmd.commands.push_back(objects_set_parent(selected_objs_copy, group.id()));

    // add the created group object
    comp_cmd.commands.push_back(
            object_creation_cmd({group.id(), group.name(), mixin_state(group, ""), true}));
    comp_cmd.commands.push_back(object_mutation_cmd(
            {group.id(), group.name(), mixin_names(group), mixin_state(group, nullptr), true}));

    // select the group and deselect the previously selected
    comp_cmd.commands.push_back(update_selection_cmd({group.id()}, selected_objs_copy));

    // add the compound command
    add_command(comp_cmd);
}

void editor::ungroup_selected() {
    auto& selected_objs = getAllMixins()["selected"].get_objects();
    if(selected_objs.empty())
        return;

    compound_cmd comp_cmd;
    comp_cmd.description = "ungrouping selected";

    for(auto& curr : selected_objs) {
        auto parent = curr.obj().get_parent();
        // skip selected objects that have no parents - they are already ungrouped
        if(!parent)
            continue;

        // record data before unparenting
        auto t          = curr.obj().get_transform();
        auto curr_old   = mixin_state(curr.obj(), "");
        auto parent_old = mixin_state(parent.obj(), "");

        // unaprent
        curr.obj().set_parent(oid::invalid());
        // set the old world transform - will update the local transform
        curr.obj().set_transform(t);

        // submit commands with data after unparenting
        comp_cmd.commands.push_back(
                attributes_changed_cmd({curr, curr.obj().name(), curr_old,
                                        mixin_state(curr.obj(), ""), "transform+parental"}));
        comp_cmd.commands.push_back(
                attributes_changed_cmd({parent, parent.obj().name(), parent_old,
                                        mixin_state(parent.obj(), ""), "parental"}));
    }

    // add the compound command
    if(comp_cmd.commands.size())
        add_command(comp_cmd);
}

void editor::duplicate_selected() {
    auto selected_objs_copy = getAllMixins()["selected"].get_objects();
    if(selected_objs_copy.empty())
        return;

    compound_cmd comp_cmd;
    comp_cmd.description = "duplicating selected";

    std::vector<oid> top_most_selected = get_top_most(selected_objs_copy);

    // deselect the previously selected objects
    comp_cmd.commands.push_back(update_selection_cmd({}, selected_objs_copy));

    std::function<oid(Object&)> copy_recursive = [&](Object& to_copy) {
        // create a new object and copy
        auto& copy = ObjectManager::get().create();
        copy.copy_from(to_copy);

        // save clean aprental state
        JsonData copy_old = mixin_state(copy, "");

        // add commands for the creation of the new copy
        comp_cmd.commands.push_back(
                object_creation_cmd({copy.id(), copy.name(), mixin_state(copy, ""), true}));
        comp_cmd.commands.push_back(object_mutation_cmd(
                {copy.id(), copy.name(), mixin_names(copy), mixin_state(copy, nullptr), true}));

        // copy children recursively
        for(auto& child_to_copy : to_copy.get_children()) {
            auto     child_copy     = copy_recursive(child_to_copy.obj());
            JsonData child_copy_old = mixin_state(child_copy.obj(), "");
            // link parentally
            child_copy.obj().set_parent(copy.id());
            // submit a command for that linking
            comp_cmd.commands.push_back(
                    attributes_changed_cmd({child_copy, child_copy.obj().name(), child_copy_old,
                                            mixin_state(child_copy.obj(), ""), "parental"}));
        }

        // update parental information for the current copy
        comp_cmd.commands.push_back(attributes_changed_cmd(
                {copy.id(), copy.name(), copy_old, mixin_state(copy, ""), "parental"}));

        return copy.id();
    };

    // copy the filtered top-most objects + all their children (and select the new top-most object copies)
    std::vector<oid> new_top_most_selected;
    for(auto& curr : top_most_selected) {
        auto new_top = copy_recursive(curr.obj());
        new_top_most_selected.push_back(new_top);

        // if the current top-most object has a parent - make the copy a child of that parent as well
        auto curr_parent = curr.obj().get_parent();
        if(curr_parent) {
            JsonData new_top_old     = mixin_state(new_top.obj(), "");
            JsonData curr_parent_old = mixin_state(curr_parent.obj(), "");
            // link parentally
            new_top.obj().set_parent(curr_parent);
            // submit a command for that linking
            comp_cmd.commands.push_back(
                    attributes_changed_cmd({new_top, new_top.obj().name(), new_top_old,
                                            mixin_state(new_top.obj(), ""), "parental"}));
            comp_cmd.commands.push_back(
                    attributes_changed_cmd({curr_parent, curr_parent.obj().name(), curr_parent_old,
                                            mixin_state(curr_parent.obj(), ""), "parental"}));
        }
    }

    // select the new top-most objects
    comp_cmd.commands.push_back(update_selection_cmd(new_top_most_selected, {}));

    // add the compound command
    add_command(comp_cmd);
}

void editor::delete_selected() {
    auto& selected_objs = getAllMixins()["selected"].get_objects();
    if(selected_objs.empty())
        return;

    handle_gizmo_changes();

    compound_cmd comp_cmd;
    comp_cmd.description = "deleting selected";

    std::function<void(oid)> delete_recursive = [&](oid root) {
        auto& root_obj = root.obj();
        // recurse through children
        for(const auto& c : root_obj.get_children())
            delete_recursive(c);

        // serialize the state of the mixins
        comp_cmd.commands.push_back(
                object_mutation_cmd({root_obj.id(), root_obj.name(), mixin_names(root_obj),
                                     mixin_state(root_obj, nullptr), false}));

        // serialize the state of the object itself
        comp_cmd.commands.push_back(
                object_creation_cmd({root, root_obj.name(), mixin_state(root_obj, ""), false}));

        ObjectManager::get().destroy(root);
    };

    std::vector<oid> top_most_selected = get_top_most(selected_objs);
    for(auto& curr : top_most_selected) {
        // first fix any parental link of the currently selected object
        auto parent = curr.obj().get_parent();
        if(parent) {
            auto parent_old = mixin_state(parent.obj(), "");
            auto curr_old   = mixin_state(curr.obj(), "");
            curr.obj().set_parent(oid());

            comp_cmd.commands.push_back(
                    attributes_changed_cmd({parent, parent.obj().name(), parent_old,
                                            mixin_state(parent.obj(), ""), "parental"}));
            comp_cmd.commands.push_back(attributes_changed_cmd(
                    {curr, curr.obj().name(), curr_old, mixin_state(curr.obj(), ""), "parental"}));
        }

        // delete recursively
        delete_recursive(curr);
    }
    add_command(comp_cmd);
}

void editor::undo_soft_commands() {
    if(soft_undo_redo_commands.size()) {
        for(auto& curr_soft : boost::adaptors::reverse(soft_undo_redo_commands))
            handle_command(curr_soft, true);
        soft_undo_redo_commands.clear();
    }
}

void editor::undo() {
    undo_soft_commands();

    if(curr_undo_redo >= 0)
        handle_command(undo_redo_commands[curr_undo_redo--], true);
}

void editor::redo() {
    undo_soft_commands();

    if(curr_undo_redo + 1 < int(undo_redo_commands.size()))
        handle_command(undo_redo_commands[++curr_undo_redo], false);
}

void editor::merge_commands() {
    int start = Utils::Min(m_selected_command_idx_1, m_selected_command_idx_2);
    int end   = Utils::Max(m_selected_command_idx_1, m_selected_command_idx_2);
    hassert(start <= end);
    hassert(start > -1);
    hassert(end < int(undo_redo_commands.size()));
    // merge only if the current position is not in the range
    if(curr_undo_redo <= start || curr_undo_redo >= end) {
        compound_cmd comp_cmd;
        comp_cmd.description = "merged commands";
        for(int i = start; i <= end; ++i)
            comp_cmd.commands.push_back(undo_redo_commands[i]);
        // add the compacted command at the place of the first from the compacted range and remove the rest of the range
        undo_redo_commands[start] = comp_cmd;
        undo_redo_commands.erase(undo_redo_commands.begin() + start + 1,
                                 undo_redo_commands.begin() + end + 1);
        if(curr_undo_redo >= end)
            curr_undo_redo -= end - start;
        // reset the command selection
        m_selected_command_idx_1 = -1;
        m_selected_command_idx_2 = -1;
    }
}

void editor::fast_forward_to_command(int idx) {
    hassert(idx < int(undo_redo_commands.size()));

    undo_soft_commands();

    while(idx != curr_undo_redo) {
        if(idx > curr_undo_redo) {
            redo();
        } else {
            if(curr_undo_redo == -1)
                break;
            undo();
        }
    }
}

void editor::handle_command(command_variant& command_var, bool undo) {
    m_should_rescroll_in_command_history = true;

    if(std::holds_alternative<attributes_changed_cmd>(command_var)) {
        auto&       cmd = std::get<attributes_changed_cmd>(command_var);
        auto&       val = undo ? cmd.old_val : cmd.new_val;
        const auto& doc = val.parse();
        hassert(doc.is_valid());
        const auto root = doc.get_root();
        hassert(root.get_length() == 1);
        if(strcmp(root.get_object_key(0).data(), "") == 0)
            deserialize(cmd.e.obj(), root.get_object_value(0)); // object attribute
        else
            common::deserialize_mixins(cmd.e.obj(), root); // mixin attribute
    } else if(std::holds_alternative<object_mutation_cmd>(command_var)) {
        auto& cmd = std::get<object_mutation_cmd>(command_var);
        if((!cmd.added && undo) || (cmd.added && !undo)) {
            // add the mixins
            for(auto& mixin : cmd.mixins)
                cmd.id.obj().addMixin(mixin.c_str());
            // deserialize the mixins
            const auto& doc = cmd.state.parse();
            hassert(doc.is_valid());
            if(cmd.id.obj().implements(common::deserialize_mixins_msg))
                common::deserialize_mixins(cmd.id.obj(), doc.get_root());
        } else {
            // remove the mixins
            for(auto& mixin : cmd.mixins)
                cmd.id.obj().remMixin(mixin.c_str());
        }
    } else if(std::holds_alternative<object_creation_cmd>(command_var)) {
        auto& cmd = std::get<object_creation_cmd>(command_var);
        if((cmd.created && undo) || (!cmd.created && !undo)) {
            ObjectManager::get().destroy(cmd.id);
        } else {
            ObjectManager::get().createFromId(cmd.id);
            const auto& doc = cmd.state.parse();
            hassert(doc.is_valid());
            deserialize(cmd.id.obj(), doc.get_root().get_object_value(0)); // object attributes
        }
    } else if(std::holds_alternative<compound_cmd>(command_var)) {
        auto& cmd = std::get<compound_cmd>(command_var);
        if(undo)
            for(auto& curr : boost::adaptors::reverse(cmd.commands))
                handle_command(curr, undo);
        else
            for(auto& curr : cmd.commands)
                handle_command(curr, undo);
    }
}

void editor::add_command(const command_variant& command, bool soft) {
    m_should_rescroll_in_command_history = true;

    if(soft && curr_undo_redo != int(undo_redo_commands.size()) - 1) {
        // if the command is soft and we are not currently at the end of the real command history - add to the soft list
        soft_undo_redo_commands.push_back(command);
    } else {
        if(!undo_redo_commands.empty()) {
            undo_redo_commands.erase(undo_redo_commands.begin() + 1 + curr_undo_redo,
                                     undo_redo_commands.end());
            // reset the command selection if it extends beyond the newly shrunken command history
            if(m_selected_command_idx_1 >= int(undo_redo_commands.size()) ||
               m_selected_command_idx_2 >= int(undo_redo_commands.size())) {
                m_selected_command_idx_1 = -1;
                m_selected_command_idx_2 = -1;
            }
        }

        // if we are adding a new command and we have soft commands - make them real
        if(soft_undo_redo_commands.size()) {
            // make a copy of the soft commands and clear them to avoid infinite recursion
            auto soft_copies = soft_undo_redo_commands;
            soft_undo_redo_commands.clear();
            for(auto& curr_soft : soft_copies)
                add_command(curr_soft);
        }

        ++curr_undo_redo;
        if(std::holds_alternative<compound_cmd>(command)) {
            const auto& cmd = std::get<compound_cmd>(command);
            if(cmd.commands.size() == 1) {
                undo_redo_commands.push_back(cmd.commands.back());
                return;
            }
        }
        undo_redo_commands.push_back(command);
    }
}

void editor::add_changed_attribute(oid e, const JsonData& old_val, const JsonData& new_val,
                                   const std::string& desc) {
    add_command(attributes_changed_cmd({e, e.obj().name(), old_val, new_val, desc}));
}

#ifndef second_h_INCLUDED
#define second_h_INCLUDED

#include "container.h"
#include "hypriso.h"

#include <any>
#include <assert.h>

#define paint [](Container *root, Container *c)
#define fz std::format
#define nz notify

#define center_y(c, in_h) c->real_bounds.y + c->real_bounds.h * .5 - in_h * .5
#define center_x(c, in_w) c->real_bounds.x + c->real_bounds.w * .5 - in_w * .5

static std::string to_lower(const std::string& str) {
    std::string result;
    result.reserve(str.size()); // avoid reallocations

    std::transform(str.begin(), str.end(), std::back_inserter(result), [](unsigned char c) { return std::tolower(c); });
    return result;
}

static bool enough_time_since_last_check(long reattempt_timeout, long last_time_checked) {
    return (get_current_time_in_ms() - last_time_checked) > reattempt_timeout;
}

enum struct TYPE : uint8_t {
    NONE = 0,
    RESIZE_HANDLE, // The handle that exists between two snapped winodws
    CLIENT_RESIZE, // The resize that exists around a window
    CLIENT, // Windows
    ALT_TAB,
    SNAP_HELPER,
    SNAP_THUMB,
    WORKSPACE_SWITCHER,
    WORKSPACE_THUMB,
};

extern std::vector<Container *> monitors;

struct Datas {
    std::unordered_map<std::string, std::any> datas;
};
extern std::unordered_map<std::string, Datas> datas;

template<typename T>
static T *get_data(const std::string& uuid, const std::string& name) {
    // Locate uuid
    auto it_uuid = datas.find(uuid);
    if (it_uuid == datas.end())
        return nullptr;

    // Locate name
    auto it_name = it_uuid->second.datas.find(name);
    if (it_name == it_uuid->second.datas.end())
        return nullptr;

    // Attempt safe cast
    if (auto ptr = std::any_cast<T>(&it_name->second))
        return ptr;

    return nullptr; // type mismatch
}

template<typename T>
static void set_data(const std::string& uuid, const std::string& name, T&& value) {
    datas[uuid].datas[name] = std::forward<T>(value);
}

template<typename T>
static T *get_or_create(const std::string& uuid, const std::string& name) {
    T *data = get_data<T>(uuid, name);
    if (!data) {
        set_data<T>(uuid, name, T());
        data = get_data<T>(uuid, name);
    }
    return data;
}

static void remove_data(const std::string& uuid) {
    datas.erase(uuid);
}

template<typename T, typename C, typename N>
auto datum(C&& container, N&& needle) {
    assert(container && "passed nullptr container to datum");
    return get_or_create<T>(std::forward<C>(container)->uuid, std::forward<N>(needle));
}

static std::tuple<int, float, int, int> from_root(Container *r) {
    int rid = *datum<int>(r, "cid"); 
    float s = scale(rid);
    int stage = *datum<int>(r, "stage"); 
    int active_id = *datum<int>(r, "active_id"); 
    return {rid, s, stage, active_id};
}

static Container *first_above_of(Container *c, TYPE type) {
    Container *client_above = nullptr; 
    Container *current = c;
    while (current->parent != nullptr) {
        if (current->parent->custom_type == (int) type) {
            return current->parent;
        }
        current = current->parent;
    }
    assert(client_above && fz("Did not find container of type {} above, probably logic bug introduced", (int) type).c_str());
    return nullptr; 
}

namespace second {    
    void begin();
    void end();
    void layout_containers();
}

#endif // second_h_INCLUDED

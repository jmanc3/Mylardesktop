#ifndef second_h_INCLUDED
#define second_h_INCLUDED

#include "container.h"
#include "hypriso.h"

#include <any>
#include <assert.h>
#include <vector>

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

class FunctionTimer {
public:
    FunctionTimer(const std::string& name) : name_(name), start_time_(std::chrono::high_resolution_clock::now()) {}

    ~FunctionTimer() {
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time_);
        notify(std::to_string(duration.count()));
        //std::cout << name_ << ": " << duration.count() << " ms" << std::endl;
    }

private:
    std::string name_;
    std::chrono::high_resolution_clock::time_point start_time_;
};

/*
template<typename T, typename C, typename N>
auto datum(C&& container, N&& needle) {
    //FunctionTimer timer("datum"); // Timer starts here
    //assert(container && "passed nullptr container to datum");
    auto a = get_or_create<T>(std::forward<C>(container)->uuid, std::forward<N>(needle));
    return a;
}
*/

struct SD {
    std::string needle;
    void *data; 
};

struct DD {
    std::string name;
    std::vector<SD *> sds;
};

static std::vector<DD *> dds;

template<typename T, typename C, typename N>
auto datum(C&& container, N&& needle) {
    //FunctionTimer timer("datum"); // Timer starts here
    //assert(container && "passed nullptr container to datum");
    auto a = get_or_create<T>(std::forward<C>(container)->uuid, std::forward<N>(needle));
    //dds->push_back();
    return a;
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
    //assert(client_above && fz("Did not find container of type {} above, probably logic bug introduced", (int) type).c_str());
    return nullptr; 
}

static void paint_debug(Container *root, Container *c) {
    border(c->real_bounds, {1, 0, 1, 1}, 4);
}

static void request_damage(Container *root, Container *c) {
    auto [rid, s, stage, active_id] = from_root(root);
    auto b = c->real_bounds;
    b.scale(1.0 / s);
    b.grow(2.0 * s);
    hypriso->damage_box(b);
}

static void consume_event(Container *root, Container *c) {
    root->consumed_event = true;
    request_damage(root, c);
}

namespace second {    
    void begin();
    void end();
    void layout_containers();
}

#endif // second_h_INCLUDED

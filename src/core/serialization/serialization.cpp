#include "serialization.h"

HAPI void serialize(int data, JsonData& out) {
    auto res = std::to_string(data);
    out.append(res.c_str(), res.length());
    //const size_t old_size = out.size();
    //out.resize(old_size + Utils::numDigits(data) + 1);
    //Utils::itoa_fast(data, out.data() + old_size);
}

HAPI void serialize(float data, JsonData& out) {
    auto res = std::to_string(data);
    out.append(res.c_str(), res.length());
}

HAPI void serialize(double data, JsonData& out) {
    auto res = std::to_string(data);
    out.append(res.c_str(), res.length());
}

HAPI void serialize(bool data, JsonData& out) {
    out.append(data ? "true" : "false", data ? 4 : 5);
}

HAPI void deserialize(int& data, const sajson::value& val) { data = val.get_integer_value(); }
HAPI void deserialize(float& data, const sajson::value& val) { data = float(val.get_double_value()); }
HAPI void deserialize(double& data, const sajson::value& val) { data = val.get_double_value(); }
HAPI void deserialize(bool& data, const sajson::value& val) {
    data = (val.get_type() == sajson::TYPE_TRUE);
}

HAPI void serialize(const glm::vec3& data, JsonData& out) {
    out.startArray();
    serialize(data.x, out);
    out.addComma();
    serialize(data.y, out);
    out.addComma();
    serialize(data.z, out);
    out.endArray();
}
HAPI void deserialize(glm::vec3& data, const sajson::value& val) {
    deserialize(data.x, val.get_array_element(0));
    deserialize(data.x, val.get_array_element(1));
    deserialize(data.x, val.get_array_element(2));
}

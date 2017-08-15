#pragma once

typedef std::vector<char> json_buf;

class JsonData
{
    json_buf m_data;

public:
    JsonData() = default;
    JsonData(json_buf&& data)
            : m_data(std::move(data)) {}

    size_t    size() const { return m_data.size(); }
    json_buf& data() { return m_data; }
    void      reserve(size_t size) { m_data.reserve(size); }

    sajson::document parse() {
        return sajson::parse(sajson::dynamic_allocation(),
                             sajson::string(m_data.data(), m_data.size()));
    }

    void addComma() { m_data.push_back(','); }
    void addNull() { m_data.push_back('\0'); }

    void startObject() { m_data.push_back('{'); }
    void startArray() { m_data.push_back('['); }

    void endObject() {
        if(m_data.back() == ',')
            m_data.back() = '}';
        else
            m_data.push_back('}');
    }

    void endArray() {
        if(m_data.back() == ',')
            m_data.back() = ']';
        else
            m_data.push_back(']');
    }

    // len should NOT include the null terminating character
    void append(const char* text, size_t len) { m_data.insert(m_data.end(), text, text + len); }

    template <size_t N>
    void append(const char (&text)[N]) {
        hassert(text[N - 1] == '\0');
        append(text, N - 1);
    }
};

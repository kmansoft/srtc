#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace srtc
{

class ExtensionMap
{
public:
    ExtensionMap();
    ~ExtensionMap();

    void add(uint8_t id, const std::string& name);

    [[nodiscard]] uint8_t findByName(const std::string& name) const;
    [[nodiscard]] std::string findById(uint8_t id) const;

    void clear();

private:
    struct Entry {
        Entry(uint8_t id, const std::string& name)
            : id(id)
            , name(name)
        {
        }

        const uint8_t id;
        const std::string name;
    };

    std::vector<Entry> mEntryList;

    mutable std::string mLastName;
    mutable uint8_t mLastId;
};

} // namespace srtc

// Tag completion for the prompt fields: Danbooru-style tags from files the
// user puts in /data/PS5SD/tags (PS5SD brings no list).
//
//   - .csv: the format of a1111-sd-webui-tagcomplete, a tag a line:
//     name,type,count,"alias1,alias2" (type and the aliases may be missing);
//   - .txt: a tag a line, optionally "name,count".
//
// A tag's underscores are spaces when it is suggested ("long_hair" is "long
// hair"), and parentheses are escaped as the prompt syntax wants them
// ("\(artist\)"). A query matches the start of a tag or of one of its aliases,
// with spaces and underscores the same; the most used tags come first.
//
// No console code in here, so ps5/app/tests builds it on the PC.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace ps5sd {

struct TagHit {
    std::string tag;    // as it goes into the prompt
    uint64_t    count = 0;
    std::string alias;  // the alias that matched, "" when the tag itself did
};

class TagIndex {
  public:
    explicit TagIndex(std::string dir) : dir_(std::move(dir)) {}

    // At most limit tags for what was typed (at least 2 characters). Reads the
    // files the first time, and again when they changed (looked at no more
    // often than every few seconds).
    std::vector<TagHit> find(const std::string & typed, size_t limit);

    // {"files": [...], "tags": <how many>} for GET /ps5sd/v1/app/tags.
    std::string info_json();

    // The hits as {"tags": [{"tag", "count", "alias"}]}.
    static std::string hits_json(const std::vector<TagHit> & hits);

    // One line of a tag file into a tag, its count and its aliases; false for
    // a line that holds none (empty, a header, a comment). Exposed for the tests.
    static bool parse_line(const std::string & line, std::string & name, uint64_t & count,
                           std::vector<std::string> & aliases);

  private:
    struct Entry {
        std::string              key;  // lower case, spaces as underscores
        std::string              tag;
        uint64_t                 count = 0;
        std::vector<std::string> alias_keys;
        std::vector<std::string> aliases;
    };

    void load_if_changed();

    std::string              dir_;
    std::mutex               mutex_;
    std::vector<Entry>       entries_;  // most used first
    std::vector<std::string> files_;
    std::string              stamp_;      // the files' names, sizes and times when they were read
    int64_t                  checked_ = -1000;  // when the folder was last looked at (seconds)
};

}  // namespace ps5sd

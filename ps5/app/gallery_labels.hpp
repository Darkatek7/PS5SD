// What the gallery keeps besides its files: a name and tags for each file,
// and the images that are not kept.
//
// The names and tags are in <outputs>/.labels.json, {"<file>": {"title":
// "...", "tags": ["...", ...]}}, so a PNG stays as it was made. The web UI's
// search finds them (GET /ps5sd/v1/app/gallery/labels); the server's own
// gallery listing does not read them.
//
// An image made with "temporary" (patches/sdcpp/0069) is saved as
// tmp_<name> in outputs: served like any other file, but not in the gallery's
// list, and deleted when the title starts (drop_temporary).
//
// No console code in here, so ps5/app/tests builds it on the PC.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace ps5sd {

struct GalleryLabel {
    std::string              title;
    std::vector<std::string> tags;
};

class GalleryLabels {
  public:
    explicit GalleryLabels(std::string outputs_dir) : dir_(std::move(outputs_dir)) {}

    // {"labels": {"<file>": {"title", "tags"}}}: every file that has a name or tags.
    std::string all_json() const;

    // Sets the name and tags of a file of the gallery: a name of one line (at
    // most 100 characters), at most 20 tags of at most 40 characters each,
    // without commas; a tag that is there already (in any case) counts once.
    // An empty name without tags removes the entry. The entries of files that
    // are gone are dropped on the way. False with a message for a file that is
    // not there or a name or tag that does not do; out is the label as kept.
    bool set(const std::string & file, const std::string & title, const std::vector<std::string> & tags,
             GalleryLabel & out, std::string & error);

    // Forgets a file's entry (its file was deleted).
    void remove(const std::string & file);

    // The file that holds them.
    std::string path() const { return dir_ + "/.labels.json"; }

  private:
    std::string        dir_;
    mutable std::mutex mutex_;
};

// Deletes the images that were not to be kept (tmp_*.png and tmp_ videos in
// outputs, with their thumbnails and parameters). Returns how many files of
// the gallery went.
int drop_temporary(const std::string & outputs_dir);

// A file of the gallery that is not kept (made with "temporary").
bool is_temporary_name(const std::string & file);

// Keeps a file that was not to be kept: tmp_<name> becomes <name>, with its
// thumbnail and parameters. False with a message for a name that is not
// tmp_..., a file that is not there, or a <name> that is taken.
bool keep_temporary(const std::string & outputs_dir, const std::string & file, std::string & kept, std::string & error);

}  // namespace ps5sd

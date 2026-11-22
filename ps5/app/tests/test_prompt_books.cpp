// Host test for prompt_books.hpp: names, limits, the entries, renaming, replacing, and the file.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#include "prompt_books.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "json.hpp"

using namespace ps5sd;
namespace fs = std::filesystem;
using S      = PromptBookStatus;

namespace {

int failures = 0;

void check(bool ok, const std::string & what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what.c_str());
        ++failures;
    }
}

nlohmann::json books_of(const PromptBooks & b) {
    return nlohmann::json::parse(b.list_json())["books"];
}

nlohmann::json one(const PromptBooks & b, const std::string & name) {
    nlohmann::json out;
    std::string    error;
    return b.get(name, out, error) == S::Ok ? out : nlohmann::json();
}

void test_names(const fs::path & dir) {
    fs::create_directories(dir);
    PromptBooks    b((dir / "promptbooks.json").u8string());
    nlohmann::json out;
    std::string    error;
    check(books_of(b).is_array() && books_of(b).empty(), "no file: no books");

    check(b.create("People", {}, out, error, 100) == S::Created && out["name"] == "People" && out["time"] == 100 &&
              out["entries"].is_array() && out["entries"].empty(),
          "create: " + error);
    check(!out.contains("next_id"), "next_id is not shown");
    check(b.create("people", {}, out, error) == S::Conflict, "a name differs in case only: taken");
    check(b.create("PEOPLE", {}, out, error) == S::Conflict, "and in upper case");
    check(one(b, "pEoPlE")["name"] == "People", "lookup without regard to case, the spelling kept");

    for (const char * bad : { "", "a b", "a/b", "ä", "a.b", "a,b", "a\n" }) {
        check(b.create(bad, {}, out, error) == S::Invalid, std::string("a bad name: '") + bad + "'");
    }
    check(b.create(std::string(41, 'a'), {}, out, error) == S::Invalid, "41 characters");
    check(b.create(std::string(40, 'a'), {}, out, error) == S::Created, "40 characters");
    check(b.create("a-b_9", {}, out, error) == S::Created, "- _ and digits");
    check(b.create("lora", {}, out, error) == S::Invalid && b.create("LoRA", {}, out, error) == S::Invalid, "lora is reserved");
    check(b.add_entry("Lora", "x", "", out, error) == S::Invalid, "lora is reserved for a new entry's book too");
    check(b.get("nothing", out, error) == S::NotFound, "no such book");

    check(books_of(b).size() == 3 && books_of(b)[0]["name"] == "People" && books_of(b)[2]["name"] == "a-b_9", "in the order made");
}

void test_entries(const fs::path & dir) {
    fs::create_directories(dir);
    PromptBooks    b((dir / "promptbooks.json").u8string());
    nlohmann::json out;
    std::string    error;

    check(b.add_entry("saved", "  a red fox  ", " blurry ", out, error, 100) == S::Created, "add makes the book: " + error);
    check(out["id"] == 1 && out["prompt"] == "a red fox" && out["negative"] == "blurry", "trimmed, id 1");
    check(one(b, "saved")["time"] == 100, "the book's time");
    check(b.add_entry("SAVED", "a blue fox", "", out, error, 101) == S::Created && out["id"] == 2, "a second, name in another case");
    check(books_of(b).size() == 1 && one(b, "saved")["time"] == 101, "one book; the time moves");

    // the same prompt and negative: the existing entry, nothing added
    check(b.add_entry("saved", "a red fox", "blurry", out, error, 200) == S::Ok && out["id"] == 1, "a duplicate returns the entry");
    check(one(b, "saved")["entries"].size() == 2 && one(b, "saved")["time"] == 101, "nothing added, time kept");
    check(b.add_entry("saved", "a red fox", "", out, error) == S::Created && out["id"] == 3, "another negative is another entry");

    check(b.add_entry("saved", "  \n", "", out, error) == S::Invalid, "a prompt of spaces");
    check(b.add_entry("saved", std::string(8001, 'a'), "", out, error) == S::Invalid, "a prompt over 8000 bytes");
    check(b.add_entry("saved", "ok", std::string(8001, 'a'), out, error) == S::Invalid, "a negative over 8000 bytes");
    check(b.add_entry("saved", std::string(8000, 'a'), std::string(8000, 'b'), out, error) == S::Created, "exactly 8000 bytes");
    check(b.add_entry("saved", "x\ny \xc3\xa4", "", out, error) == S::Created && out["prompt"] == "x\ny \xc3\xa4", "line breaks, UTF-8");

    // ids are never reused, also after the last entry went
    const int64_t last = out["id"];
    check(b.remove_entry("saved", last, error, 300) == S::Ok, "remove the last entry");
    check(b.remove_entry("saved", last, error) == S::NotFound, "once only");
    check(b.add_entry("saved", "after", "", out, error) == S::Created && out["id"].get<int64_t>() == last + 1, "the id is not reused");
    check(b.remove_entry("nope", 1, error) == S::NotFound, "entry of no book");

    // edit
    check(b.edit_entry("saved", 1, nullptr, nullptr, out, error) == S::Ok, "an edit of nothing is fine here");
    std::string np = " new ", nn = "";
    check(b.edit_entry("saved", 1, &np, &nn, out, error) == S::Ok && out["prompt"] == "new" && out["negative"] == "", "edit both");
    np = "   ";
    check(b.edit_entry("saved", 1, &np, nullptr, out, error) == S::Invalid, "an empty prompt on edit");
    nn = "neg";
    check(b.edit_entry("saved", 1, nullptr, &nn, out, error) == S::Ok && out["prompt"] == "new" && out["negative"] == "neg",
          "edit the negative only");
    check(b.edit_entry("saved", 999, &nn, nullptr, out, error) == S::NotFound, "edit of no entry");
    check(b.edit_entry("nope", 1, &nn, nullptr, out, error) == S::NotFound, "edit in no book");

    // a new instance on the same file
    PromptBooks again((dir / "promptbooks.json").u8string());
    check(again.list_json() == b.list_json(), "kept in the file");
    check(again.add_entry("saved", "more", "", out, error) == S::Created && out["id"].get<int64_t>() == last + 2,
          "next ids survive a restart");
    check(!fs::exists(dir / "promptbooks.json.tmp"), "no temporary file is left");
}

void test_update(const fs::path & dir) {
    fs::create_directories(dir);
    PromptBooks    b((dir / "promptbooks.json").u8string());
    nlohmann::json out;
    std::string    error;
    check(b.create("one", { { "a", "" }, { "b", "nb" }, { " c ", "" } }, out, error) == S::Created && out["entries"].size() == 3 &&
              out["entries"][2]["prompt"] == "c" && out["entries"][2]["id"] == 3,
          "create with entries");
    check(b.create("bad", { { "a", "" }, { " ", "" } }, out, error) == S::Invalid && books_of(b).size() == 1, "a bad entry: no book");
    b.create("two", {}, out, error);

    std::string to = "Two";
    check(b.update("one", &to, nullptr, out, error) == S::Conflict, "rename to a taken name (any case)");
    to = "TWO";
    check(b.update("two", &to, nullptr, out, error, 500) == S::Ok && out["name"] == "TWO" && out["time"] == 500,
          "a case-only rename of itself is fine");
    to = "lora";
    check(b.update("one", &to, nullptr, out, error) == S::Invalid, "rename to lora");
    to = "bad name";
    check(b.update("one", &to, nullptr, out, error) == S::Invalid, "rename to a bad name");
    to = "uno";
    std::vector<PromptBookEntry> e = { { "x", "" }, { "y", "" } };
    check(b.update("ONE", &to, &e, out, error) == S::Ok && out["name"] == "uno" && out["entries"].size() == 2 &&
              out["entries"][0]["id"] == 1 && out["entries"][1]["id"] == 2 && out["entries"][0]["prompt"] == "x",
          "rename and replace: ids from 1");
    to = "TWO";
    std::vector<PromptBookEntry> bad = { { "", "" } };
    check(b.update("uno", &to, &bad, out, error) == S::Invalid && one(b, "uno")["entries"].size() == 2, "a refused part changes nothing");
    check(b.update("uno", nullptr, nullptr, out, error) == S::Ok, "nothing to change is fine here");
    check(b.update("nope", nullptr, &e, out, error) == S::NotFound, "update of no book");
    check(b.add_entry("uno", "z", "", out, error) == S::Created && out["id"] == 3, "ids go on after a replace");
    check(books_of(b)[0]["name"] == "uno", "the order stays");

    std::string name = "UNO";
    check(b.remove_book(name, error) == S::Ok && name == "uno" && books_of(b).size() == 1, "remove: the spelling comes back");
    check(b.remove_book(name, error) == S::NotFound, "once only");
    check(b.create("uno", {}, out, error) == S::Created && out["entries"].empty(), "the name is free again");
}

void test_limits(const fs::path & dir) {
    fs::create_directories(dir);
    PromptBooks    b((dir / "promptbooks.json").u8string());
    nlohmann::json out;
    std::string    error;
    for (int i = 0; i < 200; i++) {
        check(b.create("b" + std::to_string(i), {}, out, error) == S::Created, "book " + std::to_string(i));
    }
    check(b.create("one-more", {}, out, error) == S::TooMany, "201st book: too many");
    check(b.add_entry("one-more", "x", "", out, error) == S::TooMany, "and by add_entry");
    check(books_of(b).size() == 200, "still 200");
    check(b.add_entry("b0", "x", "", out, error) == S::Created, "the 200 are usable");

    std::vector<PromptBookEntry> many;
    for (int i = 0; i < 2000; i++) {
        many.push_back({ "p" + std::to_string(i), "" });
    }
    std::vector<PromptBookEntry> none;
    check(b.update("b1", nullptr, &many, out, error) == S::Ok && out["entries"].size() == 2000, "2000 entries by replace");
    check(b.add_entry("b1", "one more", "", out, error) == S::TooMany, "the 2001st entry: too many");
    check(b.add_entry("b1", "p5", "", out, error) == S::Ok, "a duplicate there is no new entry, so it is fine");
    many.push_back({ "p2000", "" });
    check(b.update("b2", nullptr, &many, out, error) == S::TooMany && one(b, "b2")["entries"].empty(), "2001 by replace: too many");
    check(b.create("b-over", {}, out, error) == S::TooMany, "no room for a book");
    std::string name = "b0";
    check(b.remove_book(name, error) == S::Ok && b.create("b-over", many, out, error) == S::TooMany, "2001 entries on create");
    check(b.create("b-over", {}, out, error) == S::Created, "a free place after a delete");
}

void test_file(const fs::path & dir) {
    fs::create_directories(dir);
    const std::string path = (dir / "promptbooks.json").u8string();
    nlohmann::json    out;
    std::string       error;
    {
        std::ofstream(path) << "{not json";
        PromptBooks b(path);
        check(books_of(b).empty(), "a broken file reads as none");
        check(b.add_entry("saved", "fresh", "", out, error) == S::Created && books_of(b).size() == 1, "the next write makes a good file");
    }
    // what is not whole is left out; the first book of a name wins
    std::ofstream(path) << R"({"books": [
        {"name": "ok", "time": 5, "entries": [{"id": 3, "prompt": "p", "x": 1}, {"id": "a", "prompt": "bad id"}, {"prompt": "no id"},
                                              7, {"id": 4, "prompt": 5}, {"id": 0, "prompt": "zero"}]},
        {"name": "OK", "entries": []}, {"name": "bad name"}, {"name": 5}, 3, {"entries": []},
        {"name": "other", "next_id": 9, "entries": []}]})";
    PromptBooks b(path);
    const auto  l = books_of(b);
    check(l.size() == 2 && l[0]["name"] == "ok" && l[1]["name"] == "other", "only whole books are read");
    check(l[0]["entries"].size() == 1 && l[0]["entries"][0]["prompt"] == "p" && l[0]["entries"][0]["negative"] == "" &&
              !l[0]["entries"][0].contains("x"),
          "only whole entries; extra fields are dropped");
    check(b.add_entry("ok", "n", "", out, error) == S::Created && out["id"] == 4, "the next id follows the highest");
    check(b.add_entry("other", "n", "", out, error) == S::Created && out["id"] == 9, "next_id from the file");

    PromptBooks nowhere((dir / "none" / "deeper" / "promptbooks.json").u8string());
    check(nowhere.add_entry("x", "y", "", out, error) == S::WriteFailed && error.rfind("could not write", 0) == 0, "an unwritable path");
    check(books_of(nowhere).empty(), "and reads as none");
}

void test_by_name(const fs::path & dir) {
    fs::create_directories(dir);
    PromptBooks    b((dir / "promptbooks.json").u8string());
    nlohmann::json out;
    std::string    error;
    b.create("People", { { "a", "x" }, { "b", "" } }, out, error);
    b.create("Empty", {}, out, error);
    const auto m = b.entries_by_name();
    check(m.size() == 2 && m.at("people").size() == 2 && m.at("people")[0].negative == "x" && m.at("empty").empty(),
          "entries_by_name: lower-case keys, all entries");
}

void test_parse() {
    std::vector<PromptBookEntry> e;
    std::string                  error;
    check(PromptBooks::parse_entries(nlohmann::json::parse(R"([{"prompt": "a"}, {"prompt": "b", "negative": "c"}])"), e, error) &&
              e.size() == 2 && e[1].negative == "c",
          "parse_entries");
    check(!PromptBooks::parse_entries(nlohmann::json::parse(R"({"prompt": "a"})"), e, error), "not a list");
    check(!PromptBooks::parse_entries(nlohmann::json::parse(R"(["a"])"), e, error), "an entry that is a string");
    check(!PromptBooks::parse_entries(nlohmann::json::parse(R"([{"prompt": 1}])"), e, error), "a prompt that is a number");
    check(!PromptBooks::parse_entries(nlohmann::json::parse(R"([{"prompt": "a", "negative": 1}])"), e, error), "a negative that is a number");
}

}  // namespace

int main() {
    const fs::path dir = fs::temp_directory_path() / "ps5sd-test-prompt-books";
    fs::remove_all(dir);
    test_names(dir / "a");
    test_entries(dir / "b");
    test_update(dir / "c");
    test_limits(dir / "d");
    test_file(dir / "e");
    test_by_name(dir / "f");
    test_parse();
    fs::remove_all(dir);
    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("prompt_books: all passed\n");
    return 0;
}

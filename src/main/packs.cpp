#include "packs.h"

#include <cctype>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>

#include "miniz.h"

#include "platform.h"
#include "settings.h"

namespace fs = std::filesystem;

fs::path aidyn::packs::find(const std::string& name) {
    std::error_code ec;
    const fs::path beside = aidyn::platform::exe_path().parent_path() / "texture-packs";
    for (const fs::path& dir : { aidyn::settings::folder() / "texture-packs", beside }) {
        const fs::path folder = dir / name, zipped = dir / (name + ".rtz");
        const bool has_folder = fs::is_directory(folder, ec), has_zip = fs::is_regular_file(zipped, ec);
        if (has_folder && has_zip) {
            // Both (a new download unpacked over an old one, say): the newer.
            return fs::last_write_time(zipped, ec) >= fs::last_write_time(folder, ec) ? zipped : folder;
        }
        if (has_folder || has_zip) {
            return has_folder ? folder : zipped;
        }
    }
    return {};
}

namespace {
    // Open .rtz files, kept open (their directories are read once): a pack is read from now and then as the game runs.
    struct Zip {
        mz_zip_archive archive{};
        bool open = false;
        ~Zip() {
            if (open) {
                mz_zip_reader_end(&archive);
            }
        }
    };
    std::mutex zips_mutex;
    std::map<fs::path, std::unique_ptr<Zip>> zips;
}

bool aidyn::packs::read(const std::string& name, const std::string& file, std::vector<uint8_t>& out) {
    const fs::path pack = find(name);
    if (pack.empty()) {
        return false;
    }
    std::error_code ec;
    if (fs::is_directory(pack, ec)) {
        std::ifstream in(pack / file, std::ios::binary);
        if (!in) {
            return false;
        }
        out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        return true;
    }
    std::lock_guard lock(zips_mutex);
    std::unique_ptr<Zip>& zip = zips[pack];
    if (!zip) {
        zip = std::make_unique<Zip>();
        zip->open = mz_zip_reader_init_file(&zip->archive, pack.string().c_str(), 0);
    }
    if (!zip->open) {
        return false;
    }
    size_t size = 0;
    void* data = mz_zip_reader_extract_file_to_heap(&zip->archive, file.c_str(), &size, 0);
    if (data == nullptr) {
        return false;
    }
    out.assign(static_cast<uint8_t*>(data), static_cast<uint8_t*>(data) + size);
    mz_free(data);
    return true;
}

std::string aidyn::packs::first_word(const std::string& name, const std::string& file) {
    std::vector<uint8_t> bytes;
    if (!read(name, file, bytes)) {
        return {};
    }
    std::string word;
    for (uint8_t c : bytes) {
        if (std::isspace(c)) {
            if (!word.empty()) {
                break;
            }
            continue;
        }
        word += static_cast<char>(c);
    }
    return word;
}

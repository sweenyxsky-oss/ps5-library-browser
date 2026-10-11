#include "ps5_library/game_metadata.hpp"
#include "ps5_library/scanner.hpp"
#include "ps5_library/index_cache.hpp"

#include <iostream>
#include <cstdlib>
#include <filesystem>
#include <fstream>

#define TEST_ASSERT(cond) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " #cond " at " << __FILE__ << ":" << __LINE__ << std::endl; \
            std::exit(1); \
        } \
    } while (0)

using namespace ps5_library;

void test_size_formatting() {
    std::cout << "[TEST] Running test_size_formatting..." << std::endl;
    TEST_ASSERT(format_file_size(0) == "0 B");
    TEST_ASSERT(format_file_size(512) == "512 B");
    TEST_ASSERT(format_file_size(1024) == "1.00 KB");
    TEST_ASSERT(format_file_size(1024 * 1024) == "1.00 MB");
    TEST_ASSERT(format_file_size(1024ULL * 1024ULL * 1024ULL * 45ULL) == "45.0 GB");
    TEST_ASSERT(format_file_size(1024ULL * 1024ULL * 1024ULL * 1024ULL * 2ULL) == "2.00 TB");
    std::cout << "  -> PASSED" << std::endl;
}

void test_title_id_validation_and_extraction() {
    std::cout << "[TEST] Running test_title_id_validation_and_extraction..." << std::endl;
    TEST_ASSERT(is_valid_title_id("PPSA01234"));
    TEST_ASSERT(is_valid_title_id("CUSA54321"));
    TEST_ASSERT(is_valid_title_id("LAPY00001"));
    TEST_ASSERT(is_valid_title_id("FAKE99999"));
    TEST_ASSERT(!is_valid_title_id("PPSA123"));      // Too short
    TEST_ASSERT(!is_valid_title_id("ABCD01234"));    // Invalid prefix
    TEST_ASSERT(!is_valid_title_id("PPSA1234A"));    // Non-digit suffix

    TEST_ASSERT(extract_title_id("Marvel.SpiderMan2.[PPSA01234].pkg") == "PPSA01234");
    TEST_ASSERT(extract_title_id("CUSA-12345_Game_Update.pkg") == "CUSA12345");
    TEST_ASSERT(extract_title_id("NoTitleIdHere.pkg").empty());
    std::cout << "  -> PASSED" << std::endl;
}

void test_param_json_parsing() {
    std::cout << "[TEST] Running test_param_json_parsing..." << std::endl;
    std::string json_sample = R"({
        "titleId": "PPSA01234",
        "category": "gd",
        "contentVersion": "01.000.000",
        "localizedParameters": {
            "defaultLanguage": "en-US",
            "en-US": {
                "titleName": "Ratchet & Clank: Rift Apart"
            }
        }
    })";

    ParamMetadata meta = parse_param_json_string(json_sample);
    TEST_ASSERT(meta.valid);
    TEST_ASSERT(meta.title_id == "PPSA01234");
    TEST_ASSERT(meta.title_name == "Ratchet & Clank: Rift Apart");
    TEST_ASSERT(meta.kind == ItemKind::Game);
    TEST_ASSERT(meta.version == "01.000.000");

    // Test patch category
    std::string patch_sample = R"({
        "title_id": "PPSA01234",
        "category": "gp",
        "version": "01.005.000",
        "titleName": "Ratchet Patch"
    })";
    ParamMetadata patch_meta = parse_param_json_string(patch_sample);
    TEST_ASSERT(patch_meta.valid);
    TEST_ASSERT(patch_meta.kind == ItemKind::Update);
    TEST_ASSERT(patch_meta.version == "01.005.000");

    // Test DLC category
    std::string dlc_sample = R"({
        "titleId": "PPSA01234",
        "category": "ac",
        "titleName": "Preorder Armor Pack"
    })";
    ParamMetadata dlc_meta = parse_param_json_string(dlc_sample);
    TEST_ASSERT(dlc_meta.valid);
    TEST_ASSERT(dlc_meta.kind == ItemKind::Dlc);
    TEST_ASSERT(dlc_meta.title_name == "Preorder Armor Pack");

    std::cout << "  -> PASSED" << std::endl;
}

void test_filename_heuristics() {
    std::cout << "[TEST] Running test_filename_heuristics..." << std::endl;
    auto meta1 = parse_filename_heuristics("Elden.Ring.[PPSA01234].[v1.05].[Update].pkg");
    TEST_ASSERT(meta1.title_id == "PPSA01234");
    TEST_ASSERT(meta1.kind == ItemKind::Update);
    TEST_ASSERT(meta1.version == "1.05");

    auto meta2 = parse_filename_heuristics("Elden.Ring.[PPSA01234].[Shadow.Of.The.Erdtree].[DLC].pkg");
    TEST_ASSERT(meta2.title_id == "PPSA01234");
    TEST_ASSERT(meta2.kind == ItemKind::Dlc);

    auto meta3 = parse_filename_heuristics("Bloodborne.[CUSA03173].pkg");
    TEST_ASSERT(meta3.title_id == "CUSA03173");
    TEST_ASSERT(meta3.kind == ItemKind::Game);

    std::cout << "  -> PASSED" << std::endl;
}

void test_scanner_with_mock_filesystem() {
    std::cout << "[TEST] Running test_scanner_with_mock_filesystem..." << std::endl;
    std::filesystem::path mock_dir = "/tmp/mock_ps5_hostapp";
    std::filesystem::remove_all(mock_dir);
    std::filesystem::create_directories(mock_dir);

    // 1. Unpacked folder game: Demon's Souls
    std::filesystem::path game1_dir = mock_dir / "DemonsSouls";
    std::filesystem::create_directories(game1_dir / "sce_sys");
    {
        std::ofstream pf(game1_dir / "sce_sys" / "param.json");
        pf << R"({
            "titleId": "PPSA00001",
            "category": "gd",
            "titleName": "Demon's Souls"
        })";
    }
    {
        // Mock cover
        std::ofstream icon(game1_dir / "sce_sys" / "icon0.png");
        icon << "PNG_DATA";
    }
    {
        // Mock eboot binary
        std::ofstream bin(game1_dir / "eboot.bin");
        bin << "ELF_MOCK_DATA_OF_SOME_SIZE";
    }

    // 2. PKG files for Game 2: Spider-Man 2
    // Base game
    {
        std::ofstream pkg1(mock_dir / "SpiderMan2.[PPSA00002].pkg");
        pkg1 << "PKG_BASE_GAME_MOCK_DATA";
    }
    // Update
    {
        std::ofstream pkg2(mock_dir / "SpiderMan2.[PPSA00002].[Update.v1.002.000].pkg");
        pkg2 << "PKG_UPDATE_MOCK_DATA";
    }
    // DLC
    {
        std::ofstream pkg3(mock_dir / "SpiderMan2.[PPSA00002].[Fly.N.Fresh.Suit].[DLC].pkg");
        pkg3 << "PKG_DLC_MOCK_DATA";
    }

    ScannerEngine scanner;
    ScanOptions options;
    options.root_path = mock_dir.string();
    options.recursive = true;

    ScanStatistics stats;
    auto entries = scanner.scan(options, nullptr, &stats);

    TEST_ASSERT(entries.size() == 2);
    TEST_ASSERT(stats.games_found == 2);
    TEST_ASSERT(stats.updates_found == 1);
    TEST_ASSERT(stats.dlcs_found == 1);

    // Verify Demon's Souls
    auto ds = IndexCache::find_by_title_id(entries, "PPSA00001");
    TEST_ASSERT(ds.has_value());
    TEST_ASSERT(ds->title == "Demon's Souls");
    TEST_ASSERT(ds->primary_format == "folder");
    TEST_ASSERT(!ds->cover_path.empty());
    TEST_ASSERT(ds->base_game.has_value());

    // Verify Spider-Man 2 grouping
    auto sm = IndexCache::find_by_title_id(entries, "PPSA00002");
    TEST_ASSERT(sm.has_value());
    TEST_ASSERT(sm->base_game.has_value());
    TEST_ASSERT(sm->updates.size() == 1);
    TEST_ASSERT(sm->dlcs.size() == 1);
    TEST_ASSERT(sm->updates[0].kind == ItemKind::Update);
    TEST_ASSERT(sm->dlcs[0].kind == ItemKind::Dlc);

    // Cleanup mock dir
    std::filesystem::remove_all(mock_dir);
    std::cout << "  -> PASSED" << std::endl;
}

void test_atomic_cache_save_and_load() {
    std::cout << "[TEST] Running test_atomic_cache_save_and_load..." << std::endl;
    std::filesystem::path cache_file = "/tmp/test_library_cache.json";
    std::filesystem::remove(cache_file);

    IndexCache cache(cache_file.string());

    // Build mock entries
    GameEntry entry;
    entry.title_id = "PPSA07000";
    entry.title = "Final Fantasy XVI";
    entry.primary_format = "pkg";
    entry.cover_path = "/hostapp/covers/PPSA07000.png";
    entry.confidence = MetadataConfidence::Verified;

    GameItem base;
    base.kind = ItemKind::Game;
    base.filename = "FF16.pkg";
    base.size_bytes = 1024ULL * 1024ULL * 1024ULL * 80ULL; // 80 GB
    base.size_known = true;
    base.format = "pkg";
    entry.base_game = base;

    GameItem dlc;
    dlc.kind = ItemKind::Dlc;
    dlc.filename = "FF16_DLC1.pkg";
    dlc.display_name = "Echoes of the Fallen";
    dlc.size_bytes = 1024ULL * 1024ULL * 1024ULL * 10ULL; // 10 GB
    dlc.size_known = true;
    dlc.format = "pkg";
    entry.dlcs.push_back(dlc);

    GameItem upd;
    upd.kind = ItemKind::Update;
    upd.filename = "FF16_Patch103.pkg";
    upd.version = "1.03";
    upd.size_bytes = 1024ULL * 1024ULL * 1024ULL * 5ULL; // 5 GB
    upd.size_known = true;
    upd.format = "pkg";
    entry.updates.push_back(upd);

    entry.calculate_totals();

    std::vector<GameEntry> original_entries = {entry};

    std::string err;
    bool save_ok = cache.save_atomic(original_entries, "/hostapp", &err);
    TEST_ASSERT(save_ok);
    TEST_ASSERT(std::filesystem::exists(cache_file));

    // Load back
    std::vector<GameEntry> loaded_entries;
    CacheHeader header;
    bool load_ok = cache.load(loaded_entries, &header, &err);
    TEST_ASSERT(load_ok);
    TEST_ASSERT(loaded_entries.size() == 1);
    TEST_ASSERT(header.schema_version == 2);
    TEST_ASSERT(header.source_root == "/hostapp");

    const auto& loaded = loaded_entries[0];
    TEST_ASSERT(loaded.title_id == "PPSA07000");
    TEST_ASSERT(loaded.title == "Final Fantasy XVI");
    TEST_ASSERT(loaded.base_game.has_value());
    TEST_ASSERT(loaded.updates.size() == 1);
    TEST_ASSERT(loaded.dlcs.size() == 1);
    TEST_ASSERT(loaded.total_size_bytes == (80ULL + 10ULL + 5ULL) * 1024ULL * 1024ULL * 1024ULL);
    TEST_ASSERT(loaded.formatted_total_size == "95.0 GB");

    // Test Search and Filters
    auto search_result = IndexCache::search(loaded_entries, "fantasy");
    TEST_ASSERT(search_result.size() == 1);

    auto search_by_dlc = IndexCache::search(loaded_entries, "Echoes");
    TEST_ASSERT(search_by_dlc.size() == 1);

    auto search_miss = IndexCache::search(loaded_entries, "Gran Turismo");
    TEST_ASSERT(search_miss.empty());

    auto filter_pkg = IndexCache::filter_by_format(loaded_entries, "pkg");
    TEST_ASSERT(filter_pkg.size() == 1);

    auto filter_exfat = IndexCache::filter_by_format(loaded_entries, "exfat");
    TEST_ASSERT(filter_exfat.empty());

    // Cleanup
    std::filesystem::remove(cache_file);
    std::cout << "  -> PASSED" << std::endl;
}

int main() {
    std::cout << "=== Running Milestone 2: Game Scanner & Cache Engine Tests ===" << std::endl;

    test_size_formatting();
    test_title_id_validation_and_extraction();
    test_param_json_parsing();
    test_filename_heuristics();
    test_scanner_with_mock_filesystem();
    test_atomic_cache_save_and_load();

    std::cout << "=== ALL MILESTONE 2 TESTS PASSED SUCCESSFULLY ===" << std::endl;
    return 0;
}

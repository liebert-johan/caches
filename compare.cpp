#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <memory>
#include <cstdint>
#include <limits>
#include <stdexcept>

#include "lfu.hpp"
#include "2q.hpp"
#include "arc.hpp"
#include "lirs_cache.hpp"

using Cache = ICache<int, int>;

// Создать кэш выбранного типа.
std::unique_ptr<Cache> make_cache(const std::string& name, int size) {
    if (name == "LFU")
        return std::unique_ptr<Cache>(new LfuCache<int, int>(size));

    if (name == "2Q")
        return std::unique_ptr<Cache>(new TwoQCache<int, int>(size));

    if (name == "ARC")
        return std::unique_ptr<Cache>(new ArcCache<int, int>(size));

    if (name == "LIRS")
        return std::unique_ptr<Cache>(new Lirs_cache<int, int>(size));

    throw std::invalid_argument("Unknown cache: " + name);
}

int fetch(const int& key, Cache* next) {
    if (next != nullptr)
        return next->lookup_update(key, fetch);

    return key;
}

struct Situation {
    std::string name;
    std::vector<int> requests;
};

void compare(
    const Situation& situation,
    const std::vector<std::string>& algorithms,
    int levels
) {
    const int l1_size = 3;
    const int l2_size = 6;
    const int l3_size = 8;

    std::cout << "\n--- " << levels << " cache level(s) ---\n";

    std::cout << "Capacities: " << l1_size;

    if (levels >= 2)
        std::cout << " -> " << l2_size;

    if (levels == 3)
        std::cout << " -> " << l3_size;

    std::cout << "\n\n";

    std::cout << std::left
              << std::setw(26) << "Combination"
              << std::setw(12) << "L1 misses";

    if (levels >= 2)
        std::cout << std::setw(12) << "L2 misses";

    if (levels == 3)
        std::cout << std::setw(12) << "L3 misses";

    std::cout << "\n";

    std::int64_t best_misses =
        std::numeric_limits<std::int64_t>::max();

    std::vector<std::string> winners;

    // Если уровня нет, соответствующий цикл выполняется один раз.
    const std::size_t second_options =
        levels >= 2 ? algorithms.size() : 1;

    const std::size_t third_options =
        levels == 3 ? algorithms.size() : 1;

    for (const auto& first : algorithms) {
        for (std::size_t second = 0; second < second_options; ++second) {
            for (std::size_t third = 0; third < third_options; ++third) {
                // Для каждой комбинации создаём новые пустые кэши.
                auto l1 = make_cache(first, l1_size);

                std::unique_ptr<Cache> l2;
                std::unique_ptr<Cache> l3;

                std::string combination = first;

                // Пока последний уровень — первый.
                Cache* last_cache = l1.get();

                if (levels >= 2) {
                    l2 = make_cache(algorithms[second], l2_size);
                    l1->set_next_layer(l2.get());

                    combination += " -> " + algorithms[second];
                    last_cache = l2.get();
                }

                if (levels == 3) {
                    l3 = make_cache(algorithms[third], l3_size);
                    l2->set_next_layer(l3.get());

                    combination += " -> " + algorithms[third];
                    last_cache = l3.get();
                }

                // Каждой комбинации подаём одинаковые запросы.
                for (int key : situation.requests) {
                    l1->lookup_update(key, fetch);
                }

                std::cout << std::setw(26) << combination
                          << std::setw(12) << l1->get_misses();

                if (levels >= 2)
                    std::cout << std::setw(12) << l2->get_misses();

                if (levels == 3)
                    std::cout << std::setw(12) << l3->get_misses();

                std::cout << "\n";

                // Выбираем победителей по промахам последнего уровня.
                const std::int64_t last_misses =
                    last_cache->get_misses();

                if (last_misses < best_misses) {
                    best_misses = last_misses;
                    winners.clear();
                    winners.push_back(combination);
                } else if (last_misses == best_misses) {
                    winners.push_back(combination);
                }
            }
        }
    }

    std::cout << "\nBest by last-level misses:\n";

    for (const auto& winner : winners) {
        std::cout << "  " << winner << "\n";
    }

    std::cout << "Minimum last-level misses: "
              << best_misses << "\n";
}

int main() {
    std::vector<std::string> algorithms = {
        "LFU", "2Q", "ARC", "LIRS"
    };

    // Числа — номера запрашиваемых объектов.
    std::vector<Situation> situations = {
        {
            "School: repeated student requests",
            {
                1, 2, 1, 3, 1, 4, 2, 1,
                3, 4, 3, 1, 2, 1, 4, 2,
                1, 3, 1, 4, 2, 3, 2, 1
            }
        },
        {
            "Shop: popular products + one-time views",
            {
                1, 2, 1, 3, 1, 2,
                10, 11, 12, 13, 14, 15, 16, 17,
                1, 2, 3, 1, 2, 1,
                18, 19, 20, 21, 22, 23, 24, 25,
                1, 2, 3, 1, 2, 1
            }
        },
        {
            "News: popular articles change",
            {
                1, 2, 3, 4, 1, 2, 3, 4,
                1, 2, 3, 4, 1, 2, 3, 4,

                11, 12, 13, 14, 11, 12, 13, 14,
                11, 12, 13, 14, 11, 12, 13, 14,
                11, 12, 13, 14, 11, 12, 13, 14
            }
        },
        {
            "Catalog: sequential browsing",
            {
                1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12,
                1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12,
                1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12
            }
        },
        {
            "Documents: repeated work + return to old files",
            {
                1, 1, 1, 1,
                2, 2, 2, 2,
                3, 3, 3, 3,
                4, 4, 4, 4,
                5, 5, 5, 5,
                6, 6, 6, 6,
                7, 7, 7, 7,
                8, 8, 8, 8,
                1, 1, 2, 2, 3, 3
            }
        }
    };

    for (const auto& situation : situations) {
        std::cout << "\n==================================================\n";
        std::cout << situation.name << "\n";
        std::cout << "Requests: "
                  << situation.requests.size() << "\n";

        compare(situation, algorithms, 1);
        compare(situation, algorithms, 2);
        compare(situation, algorithms, 3);
    }

    return 0;
}

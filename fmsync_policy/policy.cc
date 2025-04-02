#include <iostream>
#include <fstream>
#include <string>
#include <regex>
#include <cassert>
#include <list>
#include <unordered_map>


using namespace std;

const int PAGE_NUM = 32 * 512;
const int MAX_ITER = 1000;

int n, m;
int cache_size; // target dirty set size.
int a[MAX_ITER][PAGE_NUM];

void parse_input(std::string filename) {
    std::ifstream file(filename);

    if (!file) {
        std::cerr << "Error: Could not open the file " << filename << std::endl;
        assert(false);
    }

    std::string line;
    std::regex binary_pattern("^[01]+$"); // Regular expression to match lines with only 0s and 1s

    while (std::getline(file, line)) {
        line = line.substr(0, line.length()-1);
        if (std::regex_match(line, binary_pattern)) {
            ++n;
            if (m == 0) {
                m = line.length();
            } else{
                assert(m == line.length());
            }

            for (int i = 0; i < m; ++i) {
                a[n][i] = line[i] - '0';
            }
        }
    }

    file.close();
}

void min_policy(int dirty_target) {
    std::unordered_map<int, std::list<int>> next_use;
    std::unordered_map<int, bool> dirty_set; // Tracks whether a page has been accessed

    for (int iter = 1; iter <= n; iter++) {
        for (size_t idx = 0; idx < m; idx++) {
            if (a[iter][idx] == 1) {
                next_use[idx].push_back(iter);
            }
        }
    }

    for (int iter = 1; iter <= n; iter++) {
        for (size_t idx = 0; idx < m; idx++) {
            if (a[iter][idx] == 1) {
                dirty_set[idx] = true;
                assert(!next_use[idx].empty());
                next_use[idx].pop_front();
            }
        }

        // evict using min.
        int count = 0;
        int prev_dirty = dirty_set.size();
        while (dirty_set.size() > dirty_target) {
            // Find the page with the farthest next use
            std::vector<std::pair<int, int>> v;
            for (int page = 0; page < m; ++page) {
                if (dirty_set.find(page) == dirty_set.end()) continue;
                v.push_back({-(next_use[page].empty() ? 1000000 : next_use[page].front()), page});
            }
            // sort v
            std::sort(v.begin(), v.end());
            // pop the last dirty_set.size() - dirty_target pages.
            for (int i = 0; i < dirty_set.size() - dirty_target; ++i) {
                int min_page = v[i].second;
                int min_distance = -v[i].first;
                assert(dirty_set[min_page]);
                dirty_set.erase(min_page);
                ++count;
            }
        }
        cout<< "iter: " << iter << " dirty_set: " << dirty_set.size() << " evict: " << count << " prev_dirty: " << prev_dirty << endl;
    }
}

void lru_policy(int dirty_target) {
    std::list<int> lru_list; // Doubly linked list to maintain LRU order
    std::unordered_map<int, std::list<int>::iterator> page_map; // Map to track page positions in LRU list
    std::unordered_map<int, bool> dirty_set; // Tracks whether a page has been accessed

    for (int iter = 1; iter <= n; iter++) {
        for (size_t idx = 0; idx < m; idx++) {
            if (a[iter][idx] == 1) {
                dirty_set[idx] = true;

                if (page_map.find(idx) != page_map.end()) {
                    lru_list.erase(page_map[idx]);
                }
                lru_list.push_front(idx);
                page_map[idx] = lru_list.begin();
            }
        }

        // evict using lru.
        int count = 0;
        int prev_dirty = dirty_set.size();
        // Error: in lru but not dirty.
        while (dirty_set.size() > dirty_target) {
            int lru_page = lru_list.back();
            lru_list.pop_back();
            assert(dirty_set[lru_page]);
            page_map.erase(lru_page);
            dirty_set.erase(lru_page);
            ++count;
        }

        cout<< "iter: " << iter << " dirty_set: " << dirty_set.size() << " evict: " << count << " prev_dirty: " << prev_dirty << endl;
    }
    puts("");
}


double value[PAGE_NUM];
int index_sort[PAGE_NUM];
const double exponential = 1;
bool compare_func(int x, int y) {
    return value[x] < value[y];
}
void poly_policy(int dirty_target) {
    std::unordered_map<int, bool> dirty_set; // Tracks whether a page has been accessed

    memset(value, 0, sizeof(value));

    for (int iter = 1; iter <= n; iter++) {
        for (size_t idx = 0; idx < m; idx++) {
            value[idx] = value[idx] * exponential + a[iter][idx];
            if (a[iter][idx] == 1) {
                dirty_set[idx] = true;
            }
        }

        int sz = 0;
        for (const auto& [key, value] : dirty_set) {
            assert(value == true);
            index_sort[sz++] = key;
        }
        std::sort(index_sort, index_sort + sz, compare_func);

        // evict using value.
        int prev_dirty = dirty_set.size();
        int evict_num = dirty_set.size() - dirty_target;
        if (evict_num < 0) evict_num = 0;
        for (int i = 0; i < evict_num; ++i) {
            int page = index_sort[i];
            assert(dirty_set[page] == true);
            dirty_set.erase(page);
        }

        cout<< "iter: " << iter << " dirty_set: " << dirty_set.size() << " evict: " << evict_num << " prev_dirty: " << prev_dirty << endl;
    }
    puts("");
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <filename> <cache_size>" << std::endl;
        return 1;
    }

    std::string filename = argv[1];
    cache_size = std::stoi(argv[2]);

    // redis working set: 9987

    parse_input(filename);

    printf("%d %d\n", n, m);

    puts("\n\n\npolicy: LFU");
    poly_policy(cache_size);
    puts("\n\n\npolicy: LRU");
    lru_policy(cache_size);
    puts("\n\n\npolicy: MIN");
    min_policy(cache_size);

    return 0;
}

#include <atomic>

#include "test.hpp"
#include "cppa/cppa.hpp"

using namespace std;
using namespace cppa;

namespace { atomic<size_t> s_error_count{0}; }

size_t cppa_error_count() {
    return s_error_count;
}

void cppa_inc_error_count() {
    ++s_error_count;
}

string cppa_fill4(size_t value) {
    string result = to_string(value);
    while (result.size() < 4) result.insert(result.begin(), '0');
    return result;
}

const char* cppa_strip_path(const char* file) {
    auto res = file;
    auto i = file;
    for (char c = *i; c != '\0'; c = *++i) {
        if (c == '/') {
            res = i + 1;
        }
    }
    return res;
}

void cppa_unexpected_message(const char* file, size_t line, cppa::any_tuple t) {
    CPPA_PRINTERRC(file, line, "unexpected message: " << to_string(t));
}

void cppa_unexpected_timeout(const char* file, size_t line) {
    CPPA_PRINTERRC(file, line, "unexpected timeout");
}

vector<string> split(const string& str, char delim, bool keep_empties) {
    using namespace std;
    vector<string> result;
    stringstream strs{str};
    string tmp;
    while (getline(strs, tmp, delim)) {
        if (!tmp.empty() || keep_empties) result.push_back(move(tmp));
    }
    return result;
}

void verbose_terminate () {
    try { throw; }
    catch (exception& e) {
        CPPA_PRINTERR("terminate called after throwing "
                      << to_verbose_string(e));
    }
    catch (...) {
        CPPA_PRINTERR("terminate called after throwing an unknown exception");
    }
    abort();
}

void set_default_test_settings() {
    set_terminate(verbose_terminate);
    cout.unsetf(ios_base::unitbuf);
}

map<string, string> get_kv_pairs(int argc, char** argv, int begin) {
    map<string, string> result;
    for (int i = begin; i < argc; ++i) {
        auto vec = split(argv[i], '=');
        if (vec.size() != 2) {
            CPPA_PRINTERR("\"" << argv[i] << "\" is not a key-value pair");
        }
        else if (result.count(vec[0]) != 0) {
            CPPA_PRINTERR("key \"" << vec[0] << "\" is already defined");
        }
        else result[vec[0]] = vec[1];
    }
    return result;
}

#pragma once

#include <clocale>
#include <string>

namespace locale_test_support {

// Switches C number formatting to a locale that writes "0,5" while it lives, as
// Qt does for a German user; active() is false when the system has none.
class CommaDecimalLocale {
public:
    CommaDecimalLocale() {
        const char* current = std::setlocale(LC_NUMERIC, nullptr);
        previous_ = current ? current : "C";
        for (const char* name : {"de_DE.UTF-8", "de_DE.utf8", "de_DE", "fr_FR.UTF-8",
                                 "fr_FR.utf8", "German_Germany.1252", "de-DE"}) {
            if (!std::setlocale(LC_NUMERIC, name)) continue;
            if (std::localeconv()->decimal_point[0] == ',') {
                active_ = true;
                return;
            }
            std::setlocale(LC_NUMERIC, previous_.c_str());
        }
    }
    ~CommaDecimalLocale() { std::setlocale(LC_NUMERIC, previous_.c_str()); }
    CommaDecimalLocale(const CommaDecimalLocale&) = delete;
    CommaDecimalLocale& operator=(const CommaDecimalLocale&) = delete;

    bool active() const { return active_; }

private:
    std::string previous_;
    bool active_ = false;
};

} // namespace locale_test_support

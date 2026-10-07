// Headless test for the pin helpers in src/IconLayout.cpp. Not part of the
// autotools build; run by hand after a normal build (src/defaults.h is generated):
//
//   g++ -w -Isrc tests/IconLayoutTest.cpp src/IconLayout.cpp src/Database.cpp \
//       src/Misc.cpp $(pkg-config --cflags --libs glib-2.0 gio-2.0) \
//       -o /tmp/IconLayoutTest && /tmp/IconLayoutTest
//
// Works in /tmp/iltest (HOME is pointed there) and exits non-zero on failure.
#include "IconLayout.h"
#include <cstdlib>
#include <cstdio>
#include <iostream>
#include <fstream>
#include <sstream>
using namespace std;

static int fails = 0;
#define CHECK(c, m) do { if (c) cout << "PASS: " << m << "\n"; else { cout << "FAIL: " << m << "\n"; fails++; } } while (0)

static string slurp(const string & p) { ifstream f(p.c_str()); stringstream s; s << f.rdbuf(); return s.str(); }

int main()
{
    system("rm -rf /tmp/iltest && mkdir -p /tmp/iltest/.config/idesktop");
    setenv("HOME", "/tmp/iltest", 1);
    unsetenv("XDG_CONFIG_HOME");
    string db = getLayoutDbPath();

    CHECK(!getLayoutPinned("/d/a.desktop"), "no layout.db: not pinned");
    seedLayoutPosition("/d/a.desktop", 10, 20);
    CHECK(!getLayoutPinned("/d/a.desktop"), "entry without Pinned: not pinned");

    setLayoutPinned("/d/a.desktop", true, 10, 20);
    CHECK(getLayoutPinned("/d/a.desktop"), "pinned after setLayoutPinned(true)");
    int x = 0, y = 0;
    CHECK(getLayoutPosition("/d/a.desktop", x, y) && x == 10 && y == 20, "position kept next to the pin");

    seedLayoutPosition("/d/a.desktop", 33, 44); // a drag elsewhere
    CHECK(getLayoutPinned("/d/a.desktop") && getLayoutPosition("/d/a.desktop", x, y) && x == 33 && y == 44,
          "saving a new position keeps the pin");

    setLayoutPinned("/d/new file.txt", true, 5, 6);
    CHECK(getLayoutPinned("/d/new file.txt") && getLayoutPosition("/d/new file.txt", x, y) && x == 5 && y == 6,
          "pinning an unknown icon creates its entry at the given position");

    setLayoutPinned("/d/a.desktop", false, 33, 44);
    CHECK(!getLayoutPinned("/d/a.desktop"), "unpinned");
    CHECK(slurp(db).find("Pinned") != string::npos, "the other icon's pin is still in the file");
    CHECK(getLayoutPosition("/d/a.desktop", x, y) && x == 33 && y == 44, "unpinning keeps the position");

    setLayoutPinned("/d/new file.txt", false, 5, 6);
    CHECK(slurp(db).find("Pinned") == string::npos, "no Pinned key left once everything is unpinned");
    setLayoutPinned("/d/never-seen", false, 1, 2); // nothing to clear: must not create an entry
    CHECK(!getLayoutPosition("/d/never-seen", x, y), "unpinning an unknown icon creates nothing");

    cout << (fails == 0 ? "ALL PASSED" : "SOME FAILED") << " (" << fails << " failures)\n";
    return fails;
}

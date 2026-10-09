// Headless test for the pin helpers in src/IconLayout.cpp. Not part of the
// autotools build; run by hand after a normal build (src/defaults.h is generated):
//
//   g++ -w -Isrc tests/IconLayoutTest.cpp src/IconLayout.cpp src/Database.cpp \
//       src/Misc.cpp $(pkg-config --cflags --libs glib-2.0 gio-2.0) \
//       -o /tmp/IconLayoutTest && /tmp/IconLayoutTest
//
// Works in /tmp/iltest (HOME is pointed there) and exits non-zero on failure.
#include "IconLayout.h"
#include "Misc.h"
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
    CHECK(!getLayoutPinned("/d/a.desktop"), "entry without Draggable: not pinned");

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
    CHECK(slurp(db).find("Draggable") != string::npos, "the other icon's pin is still in the file");
    CHECK(getLayoutPosition("/d/a.desktop", x, y) && x == 33 && y == 44, "unpinning keeps the position");

    setLayoutPinned("/d/new file.txt", false, 5, 6);
    CHECK(slurp(db).find("Draggable") == string::npos, "no Draggable key left once everything is unpinned");
    setLayoutPinned("/d/never-seen", false, 1, 2); // nothing to clear: must not create an entry
    CHECK(!getLayoutPosition("/d/never-seen", x, y), "unpinning an unknown icon creates nothing");

    // an entry written by the first version ("Pinned: true") still counts
    { ofstream o(db.c_str(), ios::app); o << "table /d/old.desktop\n  X: 1\n  Y: 2\n  Pinned: true\nend\n"; }
    CHECK(getLayoutPinned("/d/old.desktop"), "legacy Pinned: true still read as pinned");
    setLayoutPinned("/d/old.desktop", false, 1, 2);
    CHECK(!getLayoutPinned("/d/old.desktop") && slurp(db).find("Pinned") == string::npos, "unpinning clears the legacy key too");

    // ---- the Trash: position remembered, then given back on restore ----
    setenv("XDG_DATA_HOME", "/tmp/iltest/data", 1);
    system("mkdir -p /tmp/iltest/data/Trash/info /tmp/iltest/data/Trash/files");
    auto countTrash = [&]() { string c = slurp(db); size_t n = 0, p = 0; while ((p = c.find("table trash:", p)) != string::npos) { n++; p++; } return n; };

    CHECK(!isInTrash("/d/t1.desktop"), "trash: empty Trash holds nothing");
    { ofstream o("/tmp/iltest/data/Trash/info/t1.desktop.trashinfo"); o << "[Trash Info]\nPath=/d/t1.desktop\nDeletionDate=2026-10-08T20:00:00\n"; }
    { ofstream o("/tmp/iltest/data/Trash/info/mi archivo.txt.trashinfo"); o << "[Trash Info]\nPath=/d/mi%20archivo%C3%B1.txt\nDeletionDate=2026-10-08T20:00:00\n"; }
    CHECK(isInTrash("/d/t1.desktop"), "trash: a .trashinfo with that Path is found");
    CHECK(isInTrash("/d/mi archivo\xC3\xB1.txt"), "trash: Path= is percent-decoded (space, UTF-8)");
    CHECK(!isInTrash("/d/other.desktop"), "trash: another path is not found");

    seedLayoutPosition("/d/t2.desktop", 300, 200);
    setLayoutPinned("/d/t2.desktop", true, 300, 200);
    rememberTrashedLayout("/d/t2.desktop", 300, 200, true);
    removeLayoutPosition("/d/t2.desktop"); // what Delete does next
    CHECK(!getLayoutPosition("/d/t2.desktop", x, y), "trash: the icon's own entry is gone after Delete");
    CHECK(slurp(db).find("table trash:/d/t2.desktop") != string::npos, "trash: but a remembered entry exists");
    CHECK(adoptTrashedLayout("/d/t2.desktop"), "trash: a restored file adopts the remembered entry");
    CHECK(getLayoutPosition("/d/t2.desktop", x, y) && x == 300 && y == 200, "trash: position given back");
    CHECK(getLayoutPinned("/d/t2.desktop"), "trash: pin given back");
    CHECK(slurp(db).find("trash:/d/t2.desktop") == string::npos, "trash: remembered entry used up");
    CHECK(!adoptTrashedLayout("/d/t2.desktop"), "trash: nothing left to adopt a second time");

    rememberTrashedLayout("/d/t3.desktop", 7, 8, false);
    CHECK(adoptTrashedLayout("/d/t3.desktop") && getLayoutPosition("/d/t3.desktop", x, y) && x == 7 && y == 8 && !getLayoutPinned("/d/t3.desktop"),
          "trash: an unpinned icon comes back unpinned");

    // the Trash still holds a file from that path: what is there now is not it
    rememberTrashedLayout("/d/t1.desktop", 11, 12, false);
    CHECK(!adoptTrashedLayout("/d/t1.desktop") && !getLayoutPosition("/d/t1.desktop", x, y),
          "trash: refused while the Trash still holds that path");
    CHECK(slurp(db).find("table trash:/d/t1.desktop") != string::npos, "trash: and the memory is kept for later");
    system("rm /tmp/iltest/data/Trash/info/t1.desktop.trashinfo");
    CHECK(adoptTrashedLayout("/d/t1.desktop") && getLayoutPosition("/d/t1.desktop", x, y) && x == 11 && y == 12,
          "trash: adopted once the .trashinfo is gone (restored)");

    // something else already took the place
    rememberTrashedLayout("/d/t4.desktop", 50, 60, false);
    seedLayoutPosition("/d/t4.desktop", 400, 500);
    CHECK(!adoptTrashedLayout("/d/t4.desktop"), "trash: an existing own entry wins");
    CHECK(getLayoutPosition("/d/t4.desktop", x, y) && x == 400 && y == 500, "trash: own entry untouched");
    CHECK(slurp(db).find("trash:/d/t4.desktop") == string::npos, "trash: the stale memory is dropped");

    // a path with a space and UTF-8 in it
    rememberTrashedLayout("/d/mi archivo\xC3\xB1.txt", 21, 22, false);
    CHECK(!adoptTrashedLayout("/d/mi archivo\xC3\xB1.txt"), "trash: path with space/UTF-8 matched against its .trashinfo");
    system("rm '/tmp/iltest/data/Trash/info/mi archivo.txt.trashinfo'");
    CHECK(adoptTrashedLayout("/d/mi archivo\xC3\xB1.txt") && getLayoutPosition("/d/mi archivo\xC3\xB1.txt", x, y) && x == 21,
          "trash: ... and adopted after the restore");

    // expiry and cap
    { ofstream o(db.c_str(), ios::app); o << "table trash:/d/ancient.txt\n  X: 1\n  Y: 1\n  When: 1\nend\n"; }
    rememberTrashedLayout("/d/fresh.txt", 2, 2, false);
    CHECK(slurp(db).find("trash:/d/ancient.txt") == string::npos && slurp(db).find("trash:/d/fresh.txt") != string::npos,
          "trash: a 30-day-old memory is dropped when another is added");
    for (int i = 0; i < 230; i++)
        rememberTrashedLayout("/d/bulk" + itos(i) + ".txt", i, i, false);
    CHECK(countTrash() == 200, "trash: never more than 200 remembered");
    CHECK(slurp(db).find("trash:/d/bulk229.txt") != string::npos && slurp(db).find("trash:/d/bulk0.txt\n") == string::npos,
          "trash: the oldest go first");
    CHECK(getLayoutPosition("/d/a.desktop", x, y) && x == 33, "trash: ordinary entries untouched by all this");

    // kiosk mode never writes
    string before = slurp(db);
    kioskMode = true;
    rememberTrashedLayout("/d/kiosk.txt", 1, 1, false);
    CHECK(slurp(db) == before, "trash: kiosk mode writes nothing");
    CHECK(!adoptTrashedLayout("/d/bulk229.txt"), "trash: kiosk mode adopts nothing");
    kioskMode = false;

    cout << (fails == 0 ? "ALL PASSED" : "SOME FAILED") << " (" << fails << " failures)\n";
    return fails;
}

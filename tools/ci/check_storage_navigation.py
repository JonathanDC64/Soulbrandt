"""Compile Storage's production navigation reducer against native row events.

No game files, input injection, or rendering. Repeated observations model draws
and confirmation between updates; native wrap supplies the fifth visual stop.
"""
from pathlib import Path
import argparse
import os
import shutil
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--cxx', default=os.environ.get('CXX') or shutil.which('clang++') or shutil.which('c++'))
args = parser.parse_args()
if not args.cxx:
    parser.error('a C++17 compiler is required (--cxx)')
root = Path(__file__).resolve().parents[2]
program = r'''
#include "des_storage_navigation.h"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
static int passed=0, failed=0;
void check(bool ok, const char* name) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", name);
    (ok ? passed : failed)++;
}
void move(DesStorageNavigation& n, int direction) {
    n.observe((n.projected() + direction + 4) % 4);
}
int main() {
    DesStorageNavigation n;
    n.observe(0);
    n.observe(1); // native update is visible before the diagnostic PAD edge
    for (int draw=0;draw<20;++draw) n.observe(n.projected());
    check(n.row==1, "native-first then delayed PAD cannot add a step");
    n.reset(); n.observe(0);
    for (int draw=0;draw<20;++draw) n.observe(0); // edge seen, no native update yet
    check(n.row==0, "PAD-first alone does not navigate");
    n.observe(1);
    for (int draw=0;draw<20;++draw) n.observe(n.projected());
    check(n.row==1, "later native update counts once");
    n.reset(); n.observe(0); n.observe(1); n.observe(n.projected());
    check(n.row==1, "coincident update and edge counts once");
    for (int draws : {0,1,2,7,50}) {
        n.reset(); n.observe(0); bool ok=true;
        for (int i=0;i<200;++i) {
            const int direction=i%8<4 ? 1 : -1;
            const int before=n.row;
            move(n,direction);
            const int want=before+direction<0 ? 0 : before+direction>4 ? 4 : before+direction;
            ok &= n.row==want;
            for (int d=0;d<draws;++d) n.observe(n.projected());
            ok &= n.row==want;
        }
        char name[80]; std::snprintf(name,sizeof name,"200 updates with %d repeated draws",draws);
        check(ok,name);
    }
    n.reset(); n.observe(0); bool rows=true;
    for (int row=1;row<=4;++row) { move(n,1); rows &= n.row==row; }
    check(rows && n.projected()==3, "all five rows including native 3-to-0 wrap");
    move(n,1); check(n.row==4, "bottom clamps without disabling native repeat");
    rows=true;
    for (int row=3;row>=0;--row) { move(n,-1); rows &= n.row==row; }
    check(rows, "all five rows in reverse");
    move(n,-1); check(n.row==0, "top clamps native reverse wrap");
    n.reset(); n.observe(0); move(n,1); move(n,1);
    check(n.row==2 && n.send(), "two rapid native events without a draw are retained");
    move(n,1);
    check(n.row==3 && !n.send() && n.command()==2, "Reorganize stays native command 2 despite cursor lag");
    const int command=n.dispatch();
    n.observe(command);
    check(n.row==3 && !n.send(), "dispatch's own row write is not a navigation event");
    n.expected=n.projected(); // publishing the next draw's projection
    move(n,1);
    check(n.row==4 && !n.send() && n.command()==3, "Auto Sort stays native command 3");
    for (int native=0;native<4;++native) {
        n.reset(); n.observe(native);
        check(n.row==0 && n.projected()==0 && !n.send(), "opening clears a previous native projection to Use");
    }
    n.reset(); n.observe(0); move(n,1); move(n,1); n.reset();
    check(!n.active() && !n.send(), "close/picker/off reset revokes Send");
    n.observe(0); check(n.row==0, "reused dialog opens without stale selection");
    n.observe(-1); n.observe(9);
    check(n.row==0 && n.expected==0, "invalid native row does not manufacture an event");
    for (int i=0;i<200;++i) n.observe(n.projected());
    check(n.row==0, "empty polls/held PAD with unchanged native row are inert");
    move(n,1); move(n,1);
    check(n.send(), "confirmation can synchronize before its first draw");
    std::printf("%d passed, %d failed\n",passed,failed);
    return failed ? 1 : 0;
}
'''
with tempfile.TemporaryDirectory(prefix='storage-navigation-') as folder:
    out = Path(folder)
    cpp = out / 'check.cpp'
    cpp.write_text(program, encoding='utf-8')
    exe = out / ('check.exe' if os.name == 'nt' else 'check')
    subprocess.run([args.cxx, '-std=c++17', '-I', str(root / 'port'), str(cpp), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)

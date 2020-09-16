#
# Copyright (c) 2020, Oracle and/or its affiliates. All rights reserved.
# DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
#
# This code is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License version 2 only, as
# published by the Free Software Foundation.  Oracle designates this
# particular file as subject to the "Classpath" exception as provided
# by Oracle in the LICENSE file that accompanied this code.
#
# This code is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
# version 2 for more details (a copy is included in the LICENSE file that
# accompanied this code).
#
# You should have received a copy of the GNU General Public License version
# 2 along with this work; if not, write to the Free Software Foundation,
# Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
#
# Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
# or visit www.oracle.com if you need additional information or have any
# questions.
#


# Produce an hs_err file that can be processed by annotate_stack.tcl
#
# Usage tclsh vc_recover_stack.tcl hs_err_pid10488.log > hs_err_pid10488_fixed.log
set comment {

Sometimes the hs_err file has no stack, but has only info like this:

Top of Stack: (sp=0x0000003be64fee68)
0x0000003be64fee68:   00007ff9d4ff182f 0000022abb117a10
0x0000003be64fee78:   00007ff9d4ff1873 00000000000005ce
0x0000003be64fee88:   0000000000000000 0000003be64feec8
0x0000003be64fee98:   0000000000000000 0000022a9f6d6255
0x0000003be64feea8:   00007ff9d4ff1495 0000022abb117a10
0x0000003be64feeb8:   0000003be64fef80 0000022a9f6d6255
0x0000003be64feec8:   0000000000000000 0000003be64fef80
0x0000003be64feed8:   00007ff986386eaa 0000003bcafefafa
0x0000003be64feee8:   000000000000034b 00000000000005ce
0x0000003be64feef8:   ffffffff00000007 0000000086183d01
0x0000003be64fef08:   00001e8560e09dd1 00000000000005ce
0x0000003be64fef18:   0000022a9ea479d0 0000022a9ea45690
0x0000003be64fef28:   0000022a9d051470 0000000000b3af51
0x0000003be64fef38:   0000000000000368 0000022a9f6d6255
0x0000003be64fef48:   00007ff9d4ff1ed3 0000022abb9e0140
0x0000003be64fef58:   0000000000000016 0000000000000000 


0x00007ff985c90000 - 0x00007ff9867ec000 	C:\Program Files\jfsStreamPresenter\runtime\bin\server\jvm.dll
0x00007ff9ecbc0000 - 0x00007ff9ecbc8000 	C:\WINDOWS\System32\PSAPI.DLL
0x00007ff9bdfc0000 - 0x00007ff9bdfc9000 	C:\WINDOWS\SYSTEM32\WSOCK32.dll
0x00007ff9ed5e0000 - 0x00007ff9ed64f000 	C:\WINDOWS\System32\WS2_32.dll
0x00007ff9e8c10000 - 0x00007ff9e8c34000 	C:\WINDOWS\SYSTEM32\WINMM.dll
0x00007ff9e5950000 - 0x00007ff9e595a000 	C:\WINDOWS\SYSTEM32\VERSION.dll
0x00007ff9e8bb0000 - 0x00007ff9e8bdd000 	C:\WINDOWS\SYSTEM32\winmmbase.dll
0x00007ff9d4ff0000 - 0x00007ff9d4ffa000 	C:\Program Files\jfsStreamPresenter\runtime\bin\jimage.dll

Let's use this info to recover the stack.

}


set fd [open [lindex $argv 0]]

set addrs {}
set ranges {}
while {![eof $fd]} {
    set line [gets $fd]
    if {[regexp {^Top of Stack:} $line]} {
        while {![eof $fd]} {
            set line [gets $fd]
            if {[regexp {^0x[0-9a-f]+: *([0-9a-f]+) ([0-9a-f]+)} $line dummy a b]} {
                lappend addrs 0x$a 0x$b
            } else {
                break;
            }
        }
    } elseif {[regexp {^(0x[0-9a-f]+) * - *(0x[0-9a-f]+).*\\([a-zA-Z0-9_]+[.]dll)} $line dummy a b dll]} {
        lappend ranges $a $b $dll
    }
}
close $fd

foreach addr $addrs {
    foreach {a b dll} $ranges {
        if {$a <= $addr && $addr <= $b} {
            puts $dll+[format 0x%x [expr $addr - $a]]
        }
    }
}

